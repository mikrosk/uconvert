/*
 * uconvert: bitmap converter into Atari ST/STE/TT/Falcon-specific format
 *
 * Copyright (c) 2022 Miro Kropacek <miro.kropacek@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

/*
 * Palette optimiser: N entries; optionally a block of K consecutive
 * entries gets new colours before every line. Every colour an entry holds
 * over a range of lines is an instance.
 *
 * Cost: the sum over all pixels of the weighted squared RGB distance to
 * the colour the pixel shows (with dithering: of the same picture blurred
 * slightly, see below).
 *
 * 1. Start: one palette from k-means; with lines, the blocks in turn
 *    (line y rewrites block (y - 1) % (N / K)), the colours of every line
 *    fitted to the lines they live on.
 * 2. Lloyd rounds: every pixel takes the nearest colour of its line, every
 *    instance the mean of its pixels.
 * 3. Colour search (Arnaud Carre, "Brute Force Colors!"): every instance
 *    in turn takes the colour of the whole grid that gives the lowest
 *    cost with all other colours fixed. With alt(p) the cost of pixel p
 *    without the instance, a colour c lowers it by sum max(0, alt - d(p, c)),
 *    so only the grid points closer to p than alt(p) need a visit.
 * 4. Schedule search (with lines): every line tries every other block
 *    start; all are scored with rough colours, the best SCHEDULE_FINALISTS
 *    again with the colour search for every colour the move touches; the
 *    best is kept if it lowers the cost. Then 2. and 3. again, until no
 *    move helps.
 * 5. Dithering: the cost becomes sum_c w_c |g * e_c|^2 (g a Gaussian of
 *    deviation BLUR_SIGMA, e = shown - original), lowered by pixels taking
 *    the entry that lowers it most and by instances moving to its optimum
 *    on the grid, starting from Floyd-Steinberg.
 */

#include "palopt.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <thread>

namespace {

using Rgb = std::array<float, 3>;

constexpr float WEIGHT[3] = { 0.5f, 1.0f, 0.45f };
constexpr float INF = std::numeric_limits<float>::max();
constexpr double CONVERGED = 1e-4;          // a round lowering the cost less than this ends a phase
constexpr int MAX_ROUNDS = 32;              // of every phase
constexpr int INIT_SAMPLES = 65536;         // pixels the single palette starts from
constexpr int LINE_INIT_SAMPLES = 4096;     // pixels the colours of a line start from
constexpr int SCHEDULE_FINALISTS = 32;      // block starts of a line scored with the colour search
constexpr int SCHEDULE_PASSES = 2;          // colour search passes over the colours a move touches
constexpr int MAX_SWEEPS = 20;              // of the schedule search
constexpr float BLUR_SIGMA = 1.0f;
constexpr size_t PARALLEL_PIXELS = 65536;   // instances with more pixels search their colour in threads

inline float dist(const float* a, const Rgb& b)
{
    const float r = a[0] - b[0], g = a[1] - b[1], bl = a[2] - b[2];
    return WEIGHT[0] * r * r + WEIGHT[1] * g * g + WEIGHT[2] * bl * bl;
}

inline int nearest(const float* px, const Rgb* colors, int n, float& d)
{
    int best = 0;
    d = INF;
    for (int i = 0; i < n; ++i) {
        const float di = dist(px, colors[i]);
        if (di < d) {
            d = di;
            best = i;
        }
    }
    return best;
}

unsigned thread_count()
{
    return std::max(1u, std::thread::hardware_concurrency());
}

// fn(i, thread) for i in 0 .. n-1
void parallel_for(size_t n, const std::function<void(size_t, unsigned)>& fn)
{
    const unsigned threads = static_cast<unsigned>(std::min<size_t>(thread_count(), n));
    if (threads <= 1) {
        for (size_t i = 0; i < n; ++i)
            fn(i, 0);
        return;
    }
    std::atomic<size_t> next(0);
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t)
        pool.emplace_back([&, t] {
            for (size_t i; (i = next++) < n;)
                fn(i, t);
        });
    for (auto& th : pool)
        th.join();
}

class Grid {
public:
    explicit Grid(int levelBits) : m_max((1 << levelBits) - 1) {}

    int max() const { return m_max; }

    uint8_t level(float v) const
    {
        const int q = static_cast<int>(std::lround(v * m_max / 255.0f));
        return static_cast<uint8_t>(std::clamp(q, 0, m_max));
    }

    float value(int q) const { return q * 255.0f / m_max; }

    Rgb snap(const float* v) const { return { value(level(v[0])), value(level(v[1])), value(level(v[2])) }; }

    Levels levels(const Rgb& c) const { return { level(c[0]), level(c[1]), level(c[2]) }; }

private:
    int m_max;
};

// gains of the grid colours in a box of the grid (every stride-th level)
struct Gains {
    std::vector<float> gain;
    std::vector<uint32_t> touched;
    int lo[3], n[3], stride;
};

class Optimizer {
public:
    Optimizer(const PaletteParams& p, const std::vector<float>& rgb)
        : W(p.width), H(p.height), N(p.entries), K(p.perLine)
        , m_grid(p.levelBits), m_rgb(rgb), m_src(rgb.data())
        , m_at(static_cast<size_t>(H) * N)
        , m_ent(static_cast<size_t>(W) * H), m_err(static_cast<size_t>(W) * H)
        , m_gains(thread_count()), m_start(std::chrono::steady_clock::now()) {}

    void run(bool dither)
    {
        init();
        assign();
        lloyd();
        progress("start", total_error());
        search_colours();
        progress("colour search", total_error());

        if (K) {
            for (int s = 0; s < MAX_SWEEPS; ++s) {
                const double before = total_error();
                const int moves = sweep();
                lloyd();
                search_colours();
                progress("schedule pass " + std::to_string(s + 1) + ", " + std::to_string(moves) + " moves", total_error());
                if (moves == 0 || total_error() > before * (1.0 - CONVERGED))
                    break;
            }
        }

        if (dither)
            blur_optimise();
    }

    PaletteResult result() const
    {
        PaletteResult res;
        res.indices = m_ent;
        for (int e = 0; e < N; ++e)
            res.palette.push_back(m_grid.levels(m_cols[e][0]));
        if (K) {
            for (int y = 0; y < H; ++y) {
                int first = 0;
                if (y > 0)
                    while (!changes(y, first))
                        ++first;
                res.lineFirst.push_back(static_cast<uint16_t>(first));
                for (int e = first; e < first + K; ++e)
                    res.lineColors.push_back(m_grid.levels(color(y, e)));
            }
        }
        return res;
    }

private:
    // ---- schedule: per entry the lines it gets a new colour on

    int end_of(int e, int j) const { return j + 1 < static_cast<int>(m_births[e].size()) ? m_births[e][j + 1] - 1 : H - 1; }
    int at(int y, int e) const { return m_at[static_cast<size_t>(y) * N + e]; }
    const Rgb& color(int y, int e) const { return m_cols[e][at(y, e)]; }
    bool changes(int y, int e) const { return y > 0 && m_births[e][at(y, e)] == y; }
    const float* src(size_t p) const { return m_src + 3 * p; }

    void index_entry(int e)
    {
        int j = 0;
        for (int y = 0; y < H; ++y) {
            while (j + 1 < static_cast<int>(m_births[e].size()) && m_births[e][j + 1] <= y)
                ++j;
            m_at[static_cast<size_t>(y) * N + e] = j;
        }
    }

    void line_palette(int y, Rgb* pal) const
    {
        for (int e = 0; e < N; ++e)
            pal[e] = color(y, e);
    }

    void progress(const std::string& what, double error) const
    {
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count();
        std::cout << "  " << what << ": error " << std::fixed << std::setprecision(2)
                  << error / (static_cast<double>(W) * H) << ", " << std::setprecision(0) << seconds << " s"
                  << std::defaultfloat << std::endl;
    }

    // ---- 1. start

    void init()
    {
        m_births.assign(N, { 0 });
        m_cols.assign(N, { Rgb{} });

        if (!K) {
            for (int e = 0; e < N; ++e)
                index_entry(e);
            init_single();
            return;
        }

        const int blocks = N / K;
        for (int y = 1; y < H; ++y) {
            const int b = (y - 1) % blocks;
            for (int e = b * K; e < b * K + K; ++e) {
                m_births[e].push_back(y);
                m_cols[e].push_back(Rgb{});
            }
        }
        for (int e = 0; e < N; ++e)
            index_entry(e);

        // line by line: its new colours (all on line 0) a k-means of the
        // pixels of the lines they live on, starting from the colours the
        // entries held before
        std::vector<Rgb> state(N);
        std::vector<const float*> sample;
        std::vector<float> gd;
        std::vector<Rgb> fresh;
        std::vector<char> chosen(N);
        for (int y = 0; y < H; ++y) {
            std::vector<int> sel;
            for (int e = 0; e < N; ++e)
                if (y == 0 || changes(y, e))
                    sel.push_back(e);
            std::fill(chosen.begin(), chosen.end(), 0);
            for (int e : sel)
                chosen[e] = 1;

            const int last = std::min(H - 1, y + std::max(1, blocks) - 1);
            const size_t pixels = static_cast<size_t>(last - y + 1) * W;
            const size_t step = std::max<size_t>(1, pixels / LINE_INIT_SAMPLES);
            sample.clear();
            gd.clear();
            for (size_t i = 0; i < pixels; i += step) {
                const float* px = &m_rgb[(static_cast<size_t>(y) * W + i) * 3];
                float d = INF;
                for (int e = 0; e < N; ++e)
                    if (!chosen[e])
                        d = std::min(d, dist(px, state[e]));
                sample.push_back(px);
                gd.push_back(d);
            }
            fresh.clear();
            for (int e : sel)
                fresh.push_back(state[e]);
            fit(sample, gd, fresh.data(), static_cast<int>(sel.size()), y == 0, 4);
            for (size_t i = 0; i < sel.size(); ++i) {
                state[sel[i]] = fresh[i];
                m_cols[sel[i]][at(y, sel[i])] = fresh[i];
            }
        }
    }

    // k-means++ start on a sample of the picture, then k-means
    void init_single()
    {
        std::vector<const float*> sample;
        const size_t pixels = static_cast<size_t>(W) * H;
        const size_t step = std::max<size_t>(1, pixels / INIT_SAMPLES);
        for (size_t p = 0; p < pixels; p += step)
            sample.push_back(&m_rgb[3 * p]);

        std::vector<Rgb> c(N);
        std::mt19937 rng(1);
        std::vector<float> d(sample.size(), INF);
        c[0] = m_grid.snap(sample[rng() % sample.size()]);
        for (int k = 1; k < N; ++k) {
            double sum = 0;
            for (size_t i = 0; i < sample.size(); ++i) {
                d[i] = std::min(d[i], dist(sample[i], c[k - 1]));
                sum += d[i];
            }
            size_t pick = rng() % sample.size();
            if (sum > 0) {
                double r = std::uniform_real_distribution<double>(0.0, sum)(rng);
                for (size_t i = 0; i < sample.size(); ++i) {
                    r -= d[i];
                    if (r <= 0) {
                        pick = i;
                        break;
                    }
                }
            }
            c[k] = m_grid.snap(sample[pick]);
        }

        std::vector<float> gd(sample.size(), INF);
        fit(sample, gd, c.data(), N, false, 10);
        for (int e = 0; e < N; ++e)
            m_cols[e][0] = c[e];
    }

    // n colours: a k-means of the pixels px they show better than gd
    // (the distance to the other colours); with seed, farthest point start
    void fit(const std::vector<const float*>& px, const std::vector<float>& gd, Rgb* colors, int n, bool seed, int iterations) const
    {
        const size_t count = px.size();
        std::vector<float> err(gd);
        if (seed) {
            for (int k = 0; k < n; ++k) {
                const size_t i = std::max_element(err.begin(), err.end()) - err.begin();
                colors[k] = m_grid.snap(px[i]);
                for (size_t j = 0; j < count; ++j)
                    err[j] = std::min(err[j], dist(px[j], colors[k]));
            }
        }
        std::vector<std::array<double, 3>> sums(n);
        std::vector<size_t> counts(n);
        for (int it = 0; it < iterations; ++it) {
            std::fill(sums.begin(), sums.end(), std::array<double, 3>{});
            std::fill(counts.begin(), counts.end(), 0);
            for (size_t j = 0; j < count; ++j) {
                float d;
                const int k = nearest(px[j], colors, n, d);
                err[j] = std::min(d, gd[j]);
                if (d < gd[j]) {
                    for (int c = 0; c < 3; ++c)
                        sums[k][c] += px[j][c];
                    counts[k]++;
                }
            }
            for (int k = 0; k < n; ++k) {
                if (counts[k]) {
                    const float mean[3] = { float(sums[k][0] / counts[k]), float(sums[k][1] / counts[k]), float(sums[k][2] / counts[k]) };
                    colors[k] = m_grid.snap(mean);
                } else {
                    const size_t i = std::max_element(err.begin(), err.end()) - err.begin();
                    if (err[i] > 0.0f) {
                        colors[k] = m_grid.snap(px[i]);
                        err[i] = 0.0f;
                    }
                }
            }
        }
    }

    // ---- 2. pixels and Lloyd rounds (against m_src)

    void assign()
    {
        parallel_for(H, [this](size_t y, unsigned) {
            std::vector<Rgb> pal(N);
            line_palette(static_cast<int>(y), pal.data());
            for (int x = 0; x < W; ++x) {
                const size_t p = y * W + x;
                m_ent[p] = nearest(src(p), pal.data(), N, m_err[p]);
            }
        });
    }

    double total_error() const
    {
        double s = 0;
        for (float e : m_err)
            s += e;
        return s;
    }

    void lloyd()
    {
        double error = total_error();
        for (int round = 0; round < MAX_ROUNDS; ++round) {
            std::vector<std::vector<std::array<double, 4>>> sums(N);
            for (int e = 0; e < N; ++e)
                sums[e].assign(m_births[e].size(), { 0, 0, 0, 0 });
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    const size_t p = static_cast<size_t>(y) * W + x;
                    auto& s = sums[m_ent[p]][at(y, m_ent[p])];
                    for (int c = 0; c < 3; ++c)
                        s[c] += src(p)[c];
                    s[3] += 1;
                }
            for (int e = 0; e < N; ++e)
                for (size_t j = 0; j < m_births[e].size(); ++j)
                    if (sums[e][j][3] > 0) {
                        const auto& s = sums[e][j];
                        const float mean[3] = { float(s[0] / s[3]), float(s[1] / s[3]), float(s[2] / s[3]) };
                        m_cols[e][j] = m_grid.snap(mean);
                    }
            assign();
            const double newError = total_error();
            const bool converged = newError > error * (1.0 - CONVERGED);
            error = newError;
            if (converged)
                break;
        }
    }

    // ---- 3. colour search

    // adds the gains of pixel px (cost alt without the instance) to g
    void accumulate(Gains& g, const float* px, float alt) const
    {
        int lo[3], hi[3];
        for (int c = 0; c < 3; ++c) {
            const float rad = std::sqrt(alt / WEIGHT[c]);
            const int top = g.lo[c] + (g.n[c] - 1) * g.stride;
            int a = static_cast<int>(std::ceil((px[c] - rad) * m_grid.max() / 255.0f));
            int b = static_cast<int>(std::floor((px[c] + rad) * m_grid.max() / 255.0f));
            a = std::max(a, g.lo[c]);
            b = std::min(b, top);
            // to the lattice of the box
            a = g.lo[c] + (a - g.lo[c] + g.stride - 1) / g.stride * g.stride;
            lo[c] = a;
            hi[c] = b;
            if (a > b)
                return;
        }
        for (int r = lo[0]; r <= hi[0]; r += g.stride) {
            const float dr = m_grid.value(r) - px[0];
            const float wr = WEIGHT[0] * dr * dr;
            const size_t ir = static_cast<size_t>((r - g.lo[0]) / g.stride) * g.n[1];
            for (int gg = lo[1]; gg <= hi[1]; gg += g.stride) {
                const float dg = m_grid.value(gg) - px[1];
                const float wrg = wr + WEIGHT[1] * dg * dg;
                if (wrg >= alt)
                    continue;
                const size_t ig = (ir + (gg - g.lo[1]) / g.stride) * g.n[2];
                for (int b = lo[2]; b <= hi[2]; b += g.stride) {
                    const float db = m_grid.value(b) - px[2];
                    const float d = wrg + WEIGHT[2] * db * db;
                    if (d < alt) {
                        const size_t idx = ig + (b - g.lo[2]) / g.stride;
                        if (g.gain[idx] == 0.0f)
                            g.touched.push_back(static_cast<uint32_t>(idx));
                        g.gain[idx] += alt - d;
                    }
                }
            }
        }
    }

    static void set_box(Gains& g, const int* lo, const int* n, int stride)
    {
        for (int c = 0; c < 3; ++c) {
            g.lo[c] = lo[c];
            g.n[c] = n[c];
        }
        g.stride = stride;
        const size_t size = static_cast<size_t>(n[0]) * n[1] * n[2];
        if (g.gain.size() < size)
            g.gain.assign(size, 0.0f);
        g.touched.clear();
    }

    // the grid colour of the largest gain over the pixels pixel(i), i < count,
    // with costs alt(i) without the instance; false if none gains
    // (parallel: the pixels split over threads, only outside of threads)
    template<typename PixelFn, typename AltFn>
    bool best_colour(size_t count, PixelFn pixel, AltFn alt, Gains& own, bool parallel, Rgb& out, float& outGain) const
    {
        auto search = [&](const int* lo, const int* n, int stride, int* q) -> bool {
            const unsigned threads = parallel && count > PARALLEL_PIXELS ? thread_count() : 1;
            if (threads > 1) {
                for (unsigned t = 0; t < threads; ++t)
                    set_box(m_gains[t], lo, n, stride);
                parallel_for(threads, [&](size_t t, unsigned) {
                    const size_t a = count * t / threads, b = count * (t + 1) / threads;
                    for (size_t i = a; i < b; ++i) {
                        const float al = alt(i);
                        if (al > 0.0f)
                            accumulate(m_gains[t], pixel(i), al);
                    }
                });
                Gains& g0 = m_gains[0];
                for (unsigned t = 1; t < threads; ++t) {
                    for (uint32_t idx : m_gains[t].touched) {
                        if (g0.gain[idx] == 0.0f)
                            g0.touched.push_back(idx);
                        g0.gain[idx] += m_gains[t].gain[idx];
                        m_gains[t].gain[idx] = 0.0f;
                    }
                    m_gains[t].touched.clear();
                }
            } else {
                set_box(own, lo, n, stride);
                for (size_t i = 0; i < count; ++i) {
                    const float al = alt(i);
                    if (al > 0.0f)
                        accumulate(own, pixel(i), al);
                }
            }
            Gains& g = threads > 1 ? m_gains[0] : own;
            uint32_t best = UINT32_MAX;
            float bestGain = 0.0f;
            for (uint32_t idx : g.touched) {
                if (g.gain[idx] > bestGain) {
                    bestGain = g.gain[idx];
                    best = idx;
                }
                g.gain[idx] = 0.0f;
            }
            g.touched.clear();
            if (best == UINT32_MAX)
                return false;
            q[2] = lo[2] + static_cast<int>(best % n[2]) * stride;
            q[1] = lo[1] + static_cast<int>((best / n[2]) % n[1]) * stride;
            q[0] = lo[0] + static_cast<int>(best / (static_cast<uint32_t>(n[1]) * n[2])) * stride;
            outGain = bestGain;
            return true;
        };

        const int max = m_grid.max();
        int q[3];
        if (max < 64) {
            const int lo[3] = { 0, 0, 0 }, n[3] = { max + 1, max + 1, max + 1 };
            if (!search(lo, n, 1, q))
                return false;
        } else {
            // every 4th level, then every level around the best of them
            const int lo[3] = { 0, 0, 0 }, n[3] = { max / 4 + 1, max / 4 + 1, max / 4 + 1 };
            if (!search(lo, n, 4, q))
                return false;
            int lo2[3], n2[3];
            for (int c = 0; c < 3; ++c) {
                lo2[c] = std::max(0, q[c] - 4);
                n2[c] = std::min(max, q[c] + 4) - lo2[c] + 1;
            }
            if (!search(lo2, n2, 1, q))
                return false;
        }
        out = { m_grid.value(q[0]), m_grid.value(q[1]), m_grid.value(q[2]) };
        return true;
    }

    // every instance in turn, until the cost stops falling
    void search_colours()
    {
        for (int round = 0; round < MAX_ROUNDS; ++round) {
            const double before = total_error();
            search_pass();
            if (total_error() > before * (1.0 - CONVERGED))
                break;
        }
    }

    // the instances in waves of disjoint line ranges: those of one wave
    // touch disjoint pixels and are searched in parallel (a single palette
    // has one instance per wave and splits its pixels over threads instead)
    void search_pass()
    {
        struct Inst { int e, j, b, d; };
        std::vector<Inst> all;
        for (int e = 0; e < N; ++e)
            for (size_t j = 0; j < m_births[e].size(); ++j)
                all.push_back({ e, static_cast<int>(j), m_births[e][j], end_of(e, static_cast<int>(j)) });
        std::stable_sort(all.begin(), all.end(), [](const Inst& a, const Inst& b) { return a.b < b.b; });

        std::vector<std::vector<Inst>> waves;
        std::vector<int> waveEnd;
        for (const Inst& i : all) {
            size_t w = 0;
            while (w < waves.size() && waveEnd[w] >= i.b)
                ++w;
            if (w == waves.size()) {
                waves.emplace_back();
                waveEnd.push_back(-1);
            }
            waves[w].push_back(i);
            waveEnd[w] = i.d;
        }

        Gains own;
        for (const auto& wave : waves) {
            if (wave.size() == 1)
                search_instance(wave[0].e, wave[0].j, own, true);
            else
                parallel_for(wave.size(), [&](size_t k, unsigned t) { search_instance(wave[k].e, wave[k].j, m_gains[t], false); });
        }
    }

    void search_instance(int e, int j, Gains& gains, bool parallel)
    {
        const int b = m_births[e][j], d = end_of(e, j);
        const size_t p0 = static_cast<size_t>(b) * W, p1 = static_cast<size_t>(d + 1) * W;
        std::vector<float> alt(p1 - p0);
        std::vector<uint8_t> altEnt(p1 - p0);
        std::vector<Rgb> pal(N);

        // the cost of every pixel without this instance
        double current = 0, base = 0;
        for (int y = b; y <= d; ++y) {
            bool havePal = false;
            for (int x = 0; x < W; ++x) {
                const size_t p = static_cast<size_t>(y) * W + x, t = p - p0;
                current += m_err[p];
                if (m_ent[p] != e) {
                    alt[t] = m_err[p];
                    altEnt[t] = m_ent[p];
                } else {
                    if (!havePal) {
                        line_palette(y, pal.data());
                        pal[e] = { 1e9f, 1e9f, 1e9f };
                        havePal = true;
                    }
                    float dd;
                    altEnt[t] = nearest(src(p), pal.data(), N, dd);
                    alt[t] = dd;
                }
                base += alt[t];
            }
        }

        Rgb c;
        float gain;
        if (!best_colour(alt.size(), [&](size_t i) { return src(p0 + i); }, [&](size_t i) { return alt[i]; },
                         gains, parallel, c, gain))
            return;
        if (base - gain >= current - 1e-3)
            return;

        m_cols[e][j] = c;
        for (size_t t = 0; t < alt.size(); ++t) {
            const size_t p = p0 + t;
            const float dd = dist(src(p), c);
            m_ent[p] = dd < alt[t] ? static_cast<uint8_t>(e) : altEnt[t];
            m_err[p] = dd < alt[t] ? dd : alt[t];
        }
    }

    // ---- 4. schedule search: line y stops rewriting the entries rem (their
    // colours before and from y merge) and rewrites the entries add instead
    // (their colours split at y); scored within the window of lines it affects

    struct Ovr { int e, b, d; Rgb c; };
    struct Eval {
        int lo = 0, hi = 0;
        std::vector<Ovr> ovr;
        std::vector<uint8_t> ent;
        std::vector<float> err;
        Gains gains;
        double delta = 0;
    };
    using Move = std::pair<std::vector<int>, std::vector<int>>;

    void eval_palette(const Eval& ev, int y, Rgb* pal, int skip = -1) const
    {
        line_palette(y, pal);
        for (const Ovr& o : ev.ovr)
            if (y >= o.b && y <= o.d)
                pal[o.e] = o.c;
        if (skip >= 0)
            pal[skip] = { 1e9f, 1e9f, 1e9f };
    }

    Rgb mean_of(int e, int b, int d, const Rgb& fallback) const
    {
        double sum[3] = { 0, 0, 0 };
        size_t n = 0;
        for (size_t p = static_cast<size_t>(b) * W; p < static_cast<size_t>(d + 1) * W; ++p)
            if (m_ent[p] == e) {
                for (int c = 0; c < 3; ++c)
                    sum[c] += src(p)[c];
                ++n;
            }
        if (!n)
            return fallback;
        const float mean[3] = { float(sum[0] / n), float(sum[1] / n), float(sum[2] / n) };
        return m_grid.snap(mean);
    }

    void evaluate(int y, const Move& move, Eval& ev, int passes) const
    {
        const std::vector<int>& rem = move.first;
        const std::vector<int>& add = move.second;
        ev.ovr.clear();
        for (int e : rem) {
            const int je = at(y, e);
            const int b = m_births[e][je - 1], d = end_of(e, je);
            ev.ovr.push_back({ e, b, d, mean_of(e, b, d, m_cols[e][je - 1]) });
        }
        // the new colours: a joint k-means over the lines all of them show on
        std::vector<Rgb> fresh(add.size());
        {
            int common = H - 1;
            std::vector<char> isAdd(N, 0);
            for (int f : add) {
                isAdd[f] = 1;
                common = std::min(common, end_of(f, at(y, f)));
            }
            std::vector<const float*> px;
            std::vector<float> gd;
            for (size_t p = static_cast<size_t>(y) * W; p < static_cast<size_t>(common + 1) * W; ++p) {
                px.push_back(src(p));
                gd.push_back(isAdd[m_ent[p]] ? INF : m_err[p]);
            }
            fit(px, gd, fresh.data(), static_cast<int>(add.size()), true, 3);
        }
        for (size_t i = 0; i < add.size(); ++i) {
            const int f = add[i], jf = at(y, f);
            ev.ovr.push_back({ f, m_births[f][jf], y - 1, m_cols[f][jf] });
            ev.ovr.push_back({ f, y, end_of(f, jf), fresh[i] });
        }
        ev.lo = y;
        ev.hi = y;
        for (const Ovr& o : ev.ovr) {
            ev.lo = std::min(ev.lo, o.b);
            ev.hi = std::max(ev.hi, o.d);
        }

        // the pixels of the window with the move applied
        const size_t n = static_cast<size_t>(ev.hi - ev.lo + 1) * W;
        ev.ent.resize(n);
        ev.err.resize(n);
        std::vector<Rgb> pal(N);
        std::vector<char> ch(N, 0);
        std::vector<int> chList;
        double before = 0;
        for (int yy = ev.lo; yy <= ev.hi; ++yy) {
            eval_palette(ev, yy, pal.data());
            chList.clear();
            for (const Ovr& o : ev.ovr)
                if (yy >= o.b && yy <= o.d && !ch[o.e]) {
                    ch[o.e] = 1;
                    chList.push_back(o.e);
                }
            for (int x = 0; x < W; ++x) {
                const size_t p = static_cast<size_t>(yy) * W + x, t = static_cast<size_t>(yy - ev.lo) * W + x;
                before += m_err[p];
                const float* v = src(p);
                if (ch[m_ent[p]]) {
                    float d;
                    ev.ent[t] = nearest(v, pal.data(), N, d);
                    ev.err[t] = d;
                } else {
                    ev.ent[t] = m_ent[p];
                    ev.err[t] = m_err[p];
                    for (int e : chList) {
                        const float d = dist(v, pal[e]);
                        if (d < ev.err[t]) {
                            ev.err[t] = d;
                            ev.ent[t] = static_cast<uint8_t>(e);
                        }
                    }
                }
            }
            for (int e : chList)
                ch[e] = 0;
        }

        for (int pass = 0; pass < passes; ++pass)
            for (size_t k = 0; k < ev.ovr.size(); ++k)
                search_window(ev, k);

        double after = 0;
        for (float e : ev.err)
            after += e;
        ev.delta = after - before;
    }

    // the colour search for colour k of ev with everything else in ev fixed
    void search_window(Eval& ev, size_t k) const
    {
        Ovr& o = ev.ovr[k];
        if (o.d < o.b)
            return;
        const size_t t0 = static_cast<size_t>(o.b - ev.lo) * W, t1 = static_cast<size_t>(o.d - ev.lo + 1) * W;
        const size_t base0 = static_cast<size_t>(ev.lo) * W;
        std::vector<float> alt(t1 - t0);
        std::vector<uint8_t> altEnt(t1 - t0);
        std::vector<Rgb> pal(N);
        double current = 0, base = 0;
        for (int y = o.b; y <= o.d; ++y) {
            bool havePal = false;
            for (int x = 0; x < W; ++x) {
                const size_t t = static_cast<size_t>(y - ev.lo) * W + x, i = t - t0;
                current += ev.err[t];
                if (ev.ent[t] != o.e) {
                    alt[i] = ev.err[t];
                    altEnt[i] = ev.ent[t];
                } else {
                    if (!havePal) {
                        eval_palette(ev, y, pal.data(), o.e);
                        havePal = true;
                    }
                    float d;
                    altEnt[i] = nearest(src(base0 + t), pal.data(), N, d);
                    alt[i] = d;
                }
                base += alt[i];
            }
        }
        Rgb c;
        float gain;
        if (!best_colour(alt.size(), [&](size_t i) { return src(base0 + t0 + i); }, [&](size_t i) { return alt[i]; },
                         ev.gains, false, c, gain))
            return;
        if (base - gain >= current - 1e-3)
            return;
        o.c = c;
        for (size_t i = 0; i < alt.size(); ++i) {
            const size_t t = t0 + i;
            const float d = dist(src(base0 + t), c);
            if (d < alt[i]) {
                ev.ent[t] = static_cast<uint8_t>(o.e);
                ev.err[t] = d;
            } else {
                ev.ent[t] = altEnt[i];
                ev.err[t] = alt[i];
            }
        }
    }

    void commit(int y, const Move& move, const Eval& ev)
    {
        size_t k = 0;
        for (int e : move.first) {
            const int je = at(y, e);
            m_cols[e][je - 1] = ev.ovr[k++].c;
            m_births[e].erase(m_births[e].begin() + je);
            m_cols[e].erase(m_cols[e].begin() + je);
            index_entry(e);
        }
        for (int f : move.second) {
            const int jf = at(y, f);
            m_cols[f][jf] = ev.ovr[k++].c;
            m_births[f].insert(m_births[f].begin() + jf + 1, y);
            m_cols[f].insert(m_cols[f].begin() + jf + 1, ev.ovr[k++].c);
            index_entry(f);
        }
        for (int yy = ev.lo; yy <= ev.hi; ++yy)
            for (int x = 0; x < W; ++x) {
                const size_t p = static_cast<size_t>(yy) * W + x, t = static_cast<size_t>(yy - ev.lo) * W + x;
                m_ent[p] = ev.ent[t];
                m_err[p] = ev.err[t];
            }
    }

    int sweep()
    {
        int moves = 0;
        std::vector<Move> cands;
        std::vector<double> rough;
        std::vector<Eval> evs;
        for (int y = 1; y < H; ++y) {
            int s = 0;
            while (!changes(y, s))
                ++s;
            cands.clear();
            for (int t = 0; t + K <= N; ++t) {
                if (t == s)
                    continue;
                Move m;
                for (int e = s; e < s + K; ++e)
                    if (e < t || e >= t + K)
                        m.first.push_back(e);
                for (int e = t; e < t + K; ++e)
                    if (e < s || e >= s + K)
                        m.second.push_back(e);
                cands.push_back(m);
            }
            if (cands.empty())
                continue;

            // all with rough colours, the best of them with the colour search
            if (cands.size() > SCHEDULE_FINALISTS) {
                rough.assign(cands.size(), 0.0);
                std::vector<Eval> scratch(thread_count());
                parallel_for(cands.size(), [&](size_t i, unsigned t) {
                    evaluate(y, cands[i], scratch[t], 0);
                    rough[i] = scratch[t].delta;
                });
                std::vector<size_t> order(cands.size());
                for (size_t i = 0; i < order.size(); ++i)
                    order[i] = i;
                std::partial_sort(order.begin(), order.begin() + SCHEDULE_FINALISTS, order.end(),
                                  [&rough](size_t a, size_t b) { return rough[a] < rough[b]; });
                std::vector<Move> top;
                for (int i = 0; i < SCHEDULE_FINALISTS; ++i)
                    top.push_back(cands[order[i]]);
                cands.swap(top);
            }
            evs.resize(cands.size());
            parallel_for(cands.size(), [&](size_t i, unsigned) { evaluate(y, cands[i], evs[i], SCHEDULE_PASSES); });

            size_t best = 0;
            for (size_t i = 1; i < evs.size(); ++i)
                if (evs[i].delta < evs[best].delta)
                    best = i;
            if (evs[best].delta < -1e-3) {
                commit(y, cands[best], evs[best]);
                ++moves;
            }
        }
        return moves;
    }

    // ---- 5. dithering

    int m_kr = 0;                       // radius of g (*) g
    std::vector<float> m_kern;          // (2 m_kr + 1)^2
    float m_k0 = 0;
    std::vector<float> m_e, m_cpe;      // W * H * 3: shown - original, (g (*) g) * that

    void bump(int y, int x, const float* d)
    {
        const int R = m_kr, D = 2 * R + 1;
        const int y0 = std::max(0, y - R), y1 = std::min(H - 1, y + R);
        const int x0 = std::max(0, x - R), x1 = std::min(W - 1, x + R);
        const size_t p = static_cast<size_t>(y) * W + x;
        for (int c = 0; c < 3; ++c)
            m_e[3 * p + c] += d[c];
        for (int yy = y0; yy <= y1; ++yy) {
            const float* kr = &m_kern[static_cast<size_t>(yy - y + R) * D + (x0 - x + R)];
            float* row = &m_cpe[3 * (static_cast<size_t>(yy) * W + x0)];
            for (int i = 0; i <= x1 - x0; ++i) {
                row[3 * i + 0] += d[0] * kr[i];
                row[3 * i + 1] += d[1] * kr[i];
                row[3 * i + 2] += d[2] * kr[i];
            }
        }
    }

    void blur_rebuild()
    {
        std::fill(m_e.begin(), m_e.end(), 0.0f);
        std::fill(m_cpe.begin(), m_cpe.end(), 0.0f);
        std::vector<Rgb> pal(N);
        for (int y = 0; y < H; ++y) {
            line_palette(y, pal.data());
            for (int x = 0; x < W; ++x) {
                const size_t p = static_cast<size_t>(y) * W + x;
                float d[3];
                for (int c = 0; c < 3; ++c)
                    d[c] = pal[m_ent[p]][c] - m_rgb[3 * p + c];
                bump(y, x, d);
            }
        }
    }

    double blur_error() const
    {
        double s = 0;
        for (size_t i = 0; i < m_e.size(); ++i)
            s += WEIGHT[i % 3] * m_e[i] * m_cpe[i];
        return s;
    }

    long pixel_sweep(int pass)
    {
        long changed = 0;
        std::vector<Rgb> pal(N);
        const bool rev = pass & 1;
        for (int yi = 0; yi < H; ++yi) {
            const int y = rev ? H - 1 - yi : yi;
            line_palette(y, pal.data());
            for (int xi = 0; xi < W; ++xi) {
                const int x = rev ? W - 1 - xi : xi;
                const size_t p = static_cast<size_t>(y) * W + x;
                const int a = m_ent[p];
                // the colour that would cancel the error seen here
                float t[3];
                for (int c = 0; c < 3; ++c)
                    t[c] = pal[a][c] - m_cpe[3 * p + c] / m_k0;
                float d;
                const int b = nearest(t, pal.data(), N, d);
                if (b != a && d < dist(t, pal[a]) * 0.999999f) {
                    float dd[3];
                    for (int c = 0; c < 3; ++c)
                        dd[c] = pal[b][c] - pal[a][c];
                    bump(y, x, dd);
                    m_ent[p] = static_cast<uint8_t>(b);
                    ++changed;
                }
            }
        }
        return changed;
    }

    // every instance to the grid colour of the lowest cost with its pixels as they are
    void colour_sweep()
    {
        const int R = m_kr, D = 2 * R + 1;
        for (int e = 0; e < N; ++e)
            for (size_t j = 0; j < m_births[e].size(); ++j) {
                const int b = m_births[e][j], dEnd = end_of(e, static_cast<int>(j));
                double S1[3] = { 0, 0, 0 }, S2 = 0;
                size_t n = 0;
                for (int y = b; y <= dEnd; ++y)
                    for (int x = 0; x < W; ++x) {
                        const size_t p = static_cast<size_t>(y) * W + x;
                        if (m_ent[p] != e)
                            continue;
                        ++n;
                        for (int c = 0; c < 3; ++c)
                            S1[c] += m_cpe[3 * p + c];
                        const int y0 = std::max(b, y - R), y1 = std::min(dEnd, y + R);
                        const int x0 = std::max(0, x - R), x1 = std::min(W - 1, x + R);
                        for (int yy = y0; yy <= y1; ++yy)
                            for (int xx = x0; xx <= x1; ++xx)
                                if (m_ent[static_cast<size_t>(yy) * W + xx] == e)
                                    S2 += m_kern[static_cast<size_t>(yy - y + R) * D + (xx - x + R)];
                    }
                if (!n)
                    continue;

                // change of the cost when the colour moves by dc: sum_c w_c (2 dc S1_c + dc^2 S2)
                const Rgb cur = m_cols[e][j];
                auto q = [&](const int* v) {
                    double r = 0;
                    for (int c = 0; c < 3; ++c) {
                        const double dc = m_grid.value(v[c]) - cur[c];
                        r += WEIGHT[c] * (2.0 * dc * S1[c] + dc * dc * S2);
                    }
                    return r;
                };
                int u[3], v[3];
                for (int c = 0; c < 3; ++c) {
                    u[c] = m_grid.level(cur[c]);
                    v[c] = m_grid.level(cur[c] - static_cast<float>(S1[c] / S2));
                }
                double qv = q(v);
                for (int it = 0; it < 256; ++it) {     // down the grid
                    int bc = -1, bs = 0;
                    double bq = qv;
                    for (int c = 0; c < 3; ++c)
                        for (int sg = -1; sg <= 1; sg += 2) {
                            int w[3] = { v[0], v[1], v[2] };
                            w[c] += sg;
                            if (w[c] < 0 || w[c] > m_grid.max())
                                continue;
                            const double qq = q(w);
                            if (qq < bq) {
                                bq = qq;
                                bc = c;
                                bs = sg;
                            }
                        }
                    if (bc < 0)
                        break;
                    v[bc] += bs;
                    qv = bq;
                }
                if (qv < -1e-3 && (v[0] != u[0] || v[1] != u[1] || v[2] != u[2])) {
                    const Rgb nc = { m_grid.value(v[0]), m_grid.value(v[1]), m_grid.value(v[2]) };
                    const float dd[3] = { nc[0] - cur[0], nc[1] - cur[1], nc[2] - cur[2] };
                    for (int y = b; y <= dEnd; ++y)
                        for (int x = 0; x < W; ++x)
                            if (m_ent[static_cast<size_t>(y) * W + x] == e)
                                bump(y, x, dd);
                    m_cols[e][j] = nc;
                }
            }
    }

    // serpentine Floyd-Steinberg with the current palettes
    void floyd_steinberg()
    {
        std::vector<float> cur(3 * W, 0.0f), next(3 * W, 0.0f), target(3 * W);
        std::vector<Rgb> pal(N);
        for (int y = 0; y < H; ++y) {
            line_palette(y, pal.data());
            const float* row = &m_rgb[static_cast<size_t>(y) * W * 3];
            for (int i = 0; i < 3 * W; ++i)
                target[i] = row[i] + cur[i];
            std::fill(next.begin(), next.end(), 0.0f);
            const bool ltr = (y & 1) == 0;
            const int fwd = ltr ? 1 : -1;
            for (int i = 0; i < W; ++i) {
                const int x = ltr ? i : W - 1 - i;
                float px[3];
                for (int c = 0; c < 3; ++c)
                    px[c] = std::clamp(target[3 * x + c], 0.0f, 255.0f);
                float d;
                const int k = nearest(px, pal.data(), N, d);
                m_ent[static_cast<size_t>(y) * W + x] = static_cast<uint8_t>(k);
                for (int c = 0; c < 3; ++c) {
                    const float e = px[c] - pal[k][c];
                    if (x + fwd >= 0 && x + fwd < W) {
                        target[3 * (x + fwd) + c] += e * 7.0f / 16.0f;
                        next[3 * (x + fwd) + c] += e * 1.0f / 16.0f;
                    }
                    if (x - fwd >= 0 && x - fwd < W)
                        next[3 * (x - fwd) + c] += e * 3.0f / 16.0f;
                    next[3 * x + c] += e * 5.0f / 16.0f;
                }
            }
            std::swap(cur, next);
        }
    }

    void blur_optimise()
    {
        // kernel g (*) g: a Gaussian of variance 2 sigma^2
        m_kr = static_cast<int>(std::ceil(3.0f * BLUR_SIGMA * std::sqrt(2.0f)));
        const int D = 2 * m_kr + 1;
        std::vector<double> k1(D);
        double sum = 0;
        for (int i = 0; i < D; ++i)
            sum += k1[i] = std::exp(-double(i - m_kr) * (i - m_kr) / (4.0 * BLUR_SIGMA * BLUR_SIGMA));
        m_kern.resize(static_cast<size_t>(D) * D);
        for (int a = 0; a < D; ++a)
            for (int b = 0; b < D; ++b)
                m_kern[static_cast<size_t>(a) * D + b] = static_cast<float>(k1[a] * k1[b] / (sum * sum));
        m_k0 = m_kern[static_cast<size_t>(m_kr) * D + m_kr];
        m_e.assign(m_rgb.size(), 0.0f);
        m_cpe.assign(m_rgb.size(), 0.0f);

        floyd_steinberg();
        blur_rebuild();

        for (int round = 0; round < MAX_ROUNDS; ++round) {
            const double before = blur_error();
            for (int s = 0; s < 4; ++s)
                if (pixel_sweep(round + s) < static_cast<long>(W) * H / 2000)
                    break;
            colour_sweep();
            blur_rebuild();     // drops the rounding the updates leave
            progress("dithering round " + std::to_string(round + 1), blur_error());
            if (blur_error() > before * (1.0 - CONVERGED))
                break;
        }
    }

    const int W, H, N, K;
    const Grid m_grid;
    const std::vector<float>& m_rgb;
    const float* m_src;

    std::vector<std::vector<int>> m_births;    // N: the lines an entry gets a new colour on, from 0
    std::vector<std::vector<Rgb>> m_cols;      // N: the colour of each
    std::vector<int> m_at;                     // H * N: the index into m_births[e] in force on line y
    std::vector<uint8_t> m_ent;                // W * H: the entry of every pixel
    std::vector<float> m_err;                  // W * H: its cost (without dithering)
    mutable std::vector<Gains> m_gains;        // per thread
    const std::chrono::steady_clock::time_point m_start;
};

} // namespace

PaletteResult optimise_palette(const PaletteParams& params, const std::vector<float>& rgb)
{
    Optimizer optimizer(params, rgb);
    optimizer.run(params.dither);
    return optimizer.result();
}
