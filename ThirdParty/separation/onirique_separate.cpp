// La séparation de la voix pour AUTO MIX : demucs.cpp sur plusieurs threads (le morceau découpé en parts qui se
// recouvrent de 0,75 s, recollées en fondu, comme threaded_inference.hpp de demucs.cpp).
#include "onirique_separate.hpp"
#include "model.hpp"
#include "tensor.hpp"
#include <Eigen/Dense>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <thread>

namespace onirique::sep
{
namespace
{
struct Cancelled {};
}

bool separateVocals (const std::string& modelFile, const float* left, const float* right, int n, int threads,
                     std::vector<float>& vocals, std::function<bool (float)> progress)
{
    if (left == nullptr || right == nullptr || n < 44100) return false;
    auto model = std::make_unique<demucscpp::demucs_model>();
    if (! demucscpp::load_demucs_model (modelFile, model.get())) return false;

    threads = std::max (1, std::min (threads, std::max (1, n / (44100 * 6))));   // au moins 6 s par part
    const int overlap = (int) std::floor (44100 * 0.75f);
    const int segLen = (int) std::ceil ((float) n / (float) threads);

    Eigen::MatrixXf full (2, n);
    for (int i = 0; i < n; ++i) { full (0, i) = left[i]; full (1, i) = right[i]; }

    std::vector<Eigen::MatrixXf> segs;
    for (int t = 0; t < threads; ++t)
    {
        const int start = t * segLen, end = std::min (n, start + segLen);
        Eigen::MatrixXf s = Eigen::MatrixXf::Zero (2, end - start + 2 * overlap);
        const int pre = std::min (overlap, start), post = std::min (overlap, n - end);
        s.block (0, overlap - pre, 2, pre + (end - start) + post) = full.block (0, start - pre, 2, pre + (end - start) + post);
        segs.push_back (std::move (s));
    }

    std::vector<Eigen::Tensor3dXf> outs ((size_t) threads);
    std::vector<float> prog ((size_t) threads, 0.0f);
    std::atomic<bool> stop { false };
    std::mutex m;
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back ([&, t]
        {
            demucscpp::ProgressCallback cb = [&, t] (float p, const std::string&)
            {
                if (stop.load()) throw Cancelled {};
                float total = 0.0f;
                {
                    std::lock_guard<std::mutex> lk (m);
                    prog[(size_t) t] = p;
                    for (float v : prog) total += v;
                }
                if (progress && ! progress (total / (float) threads)) { stop.store (true); throw Cancelled {}; }
            };
            try { outs[(size_t) t] = demucscpp::demucs_inference (*model, segs[(size_t) t], cb); }
            catch (...) { stop.store (true); }
        });
    for (auto& th : pool) th.join();
    if (stop.load()) return false;

    // recollage : chaque part pondérée par une rampe triangulaire, la piste voix (index 3) en mono
    vocals.assign ((size_t) n, 0.0f);
    std::vector<float> weight ((size_t) n, 0.0f);
    for (int t = 0; t < threads; ++t)
    {
        const auto& o = outs[(size_t) t];
        if (o.dimension (0) < 4) return false;
        const int start = t * segLen, end = std::min (n, start + segLen), len = end - start;
        const int pre = std::min (overlap, start), post = std::min (overlap, n - end);
        const int from = start - pre, count = pre + len + post;
        for (int i = 0; i < count; ++i)
        {
            const int src = overlap - pre + i;
            if (src < 0 || src >= (int) o.dimension (2)) continue;
            const float w = (float) std::min (i + 1, count - i);
            vocals[(size_t) (from + i)] += w * 0.5f * (o (3, 0, src) + o (3, 1, src));
            weight[(size_t) (from + i)] += w;
        }
    }
    for (int i = 0; i < n; ++i) if (weight[(size_t) i] > 0.0f) vocals[(size_t) i] /= weight[(size_t) i];
    return true;
}
} // namespace onirique::sep
