#include "Tessellation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <numbers>
#include <numeric>
#include <span>
#include <unordered_map>

namespace Fenrir::PainterDetail
{
    namespace
    {
        using Math::Vec2;

        constexpr float Pi = std::numbers::pi_v<float>;
        constexpr float TwoPi = 2.0f * Pi;
        /** @brief Points are snapped to a grid of this many cells a unit, so that edges meeting at a point share it. */
        constexpr float Grid = 256.0f;
        /** @brief The most lines one curve is split into. */
        constexpr int MaxPieces = 1024;
        /** @brief The furthest a fringe vertex moves at a sharp corner, in multiples of the half fringe. */
        constexpr float MaxMiter = 5.0f;
        /** @brief The length a dash of none is given, so its caps know which way they face. */
        constexpr float DotLength = 1.0f / 64.0f;

        float Cross(Vec2 a, Vec2 b)
        {
            return a.x * b.y - a.y * b.x;
        }

        float Dot(Vec2 a, Vec2 b)
        {
            return a.x * b.x + a.y * b.y;
        }

        float Length(Vec2 v)
        {
            return std::sqrt(Dot(v, v));
        }

        /** @brief The side a fill's boundary keeps what it covers on, and a stroke's left: `d` turned a quarter. */
        Vec2 Left(Vec2 d)
        {
            return {-d.y, d.x};
        }

        Vec2 Right(Vec2 d)
        {
            return {d.y, -d.x};
        }

        Vec2 Rotated(Vec2 v, float radians)
        {
            const float c = std::cos(radians);
            const float s = std::sin(radians);
            return {v.x * c - v.y * s, v.x * s + v.y * c};
        }

        /** @brief Appends `point` unless it is where the contour already ends. */
        void AddPoint(std::vector<Vec2>& points, Vec2 point)
        {
            if (points.empty())
            {
                points.push_back(point);
                return;
            }
            const Vec2 d = point - points.back();
            if (Dot(d, d) > 1e-10f)
            {
                points.push_back(point);
            }
        }

        /** @brief Lines enough that a curve whose error over one line is `measure` strays at most `tolerance`. */
        int Pieces(float measure, float tolerance)
        {
            const float n = std::ceil(std::sqrt(measure / std::max(tolerance, 1e-4f)));
            return std::clamp(static_cast<int>(n), 1, MaxPieces);
        }

        /** @brief How many lines an arc of `radius` through `radians` needs to stray at most `tolerance`. */
        int ArcPieces(float radius, float radians, float tolerance)
        {
            const float ratio = std::clamp(1.0f - tolerance / std::max(radius, 1e-6f), -1.0f, 1.0f);
            const float step = std::max(2.0f * std::acos(ratio), 1e-3f);
            return std::clamp(static_cast<int>(std::ceil(std::abs(radians) / step)), 1, MaxPieces);
        }

        // --- Strokes ---------------------------------------------------------------------------------------------

        /**
         * @brief The outer side of a join at `p`, from `p + n0 halfWidth` to `p + n1 halfWidth`. `bulge` is +1 or -1,
         * the way a half-turn's round join turns from n0.
         */
        void OuterJoin(std::vector<Vec2>& points, Vec2 p, Vec2 n0, Vec2 n1, float halfWidth, const StrokeStyle& style,
                       float bulge, float tolerance)
        {
            AddPoint(points, p + n0 * halfWidth);
            const float cosine = std::clamp(Dot(n0, n1), -1.0f, 1.0f);
            if (style.join == LineJoin::Miter)
            {
                // The miter is 1 / cos(half the turn) half widths long, SVG's miterLength / width.
                const float cosHalf = std::sqrt(std::max((1.0f + cosine) * 0.5f, 0.0f));
                if (cosHalf > 1e-6f && 1.0f / cosHalf <= style.miterLimit)
                {
                    const Vec2 sum = n0 + n1;
                    AddPoint(points, p + sum * (halfWidth / (Length(sum) * cosHalf)));
                }
            }
            else if (style.join == LineJoin::Round)
            {
                float angle = std::atan2(Cross(n0, n1), cosine);
                if (std::abs(Cross(n0, n1)) < 1e-6f && cosine < 0.0f)
                {
                    angle = Pi * bulge;
                }
                const int pieces = ArcPieces(halfWidth, angle, tolerance);
                for (int k = 1; k < pieces; ++k)
                {
                    AddPoint(points,
                             p + Rotated(n0, angle * static_cast<float>(k) / static_cast<float>(pieces)) * halfWidth);
                }
            }
            AddPoint(points, p + n1 * halfWidth);
        }

        /**
         * @brief The join at `p` from direction `d0` to `d1`: the outer side's join and the inner side through `p`
         * itself, so that the outline is the sum of each segment's rectangle and the join, all wound one way.
         */
        void Join(std::vector<Vec2>& left, std::vector<Vec2>& right, Vec2 p, Vec2 d0, Vec2 d1, float halfWidth,
                  const StrokeStyle& style, float tolerance)
        {
            const float turn = Cross(d0, d1);
            const float along = Dot(d0, d1);
            if (std::abs(turn) < 1e-6f && along > 0.0f)
            {
                AddPoint(left, p + Left(d1) * halfWidth);
                AddPoint(right, p + Right(d1) * halfWidth);
                return;
            }
            // Turning towards the left side, the right is outside the bend; a half turn takes the right.
            if (turn > 0.0f || (std::abs(turn) < 1e-6f && along < 0.0f))
            {
                OuterJoin(right, p, Right(d0), Right(d1), halfWidth, style, 1.0f, tolerance);
                AddPoint(left, p + Left(d0) * halfWidth);
                AddPoint(left, p);
                AddPoint(left, p + Left(d1) * halfWidth);
            }
            else
            {
                OuterJoin(left, p, Left(d0), Left(d1), halfWidth, style, -1.0f, tolerance);
                AddPoint(right, p + Right(d0) * halfWidth);
                AddPoint(right, p);
                AddPoint(right, p + Right(d1) * halfWidth);
            }
        }

        /** @brief The cap at `p`, the end of a stroke heading `d`: from `p + Left(d) halfWidth` round to `p + Right(d)
         * halfWidth`. */
        void Cap(std::vector<Vec2>& points, Vec2 p, Vec2 d, float halfWidth, LineCap cap, float tolerance)
        {
            if (cap == LineCap::Square)
            {
                AddPoint(points, p + (Left(d) + d) * halfWidth);
                AddPoint(points, p + (Right(d) + d) * halfWidth);
            }
            else if (cap == LineCap::Round)
            {
                const int pieces = std::max(ArcPieces(halfWidth, Pi, tolerance), 2);
                for (int k = 1; k < pieces; ++k)
                {
                    AddPoint(points, p + Rotated(Left(d), -Pi * static_cast<float>(k) / static_cast<float>(pieces)) *
                                             halfWidth);
                }
            }
        }

        /** @brief The unit directions of the segments of `points`, closing back to the first when `closed`. */
        std::vector<Vec2> Directions(const std::vector<Vec2>& points, bool closed)
        {
            const std::size_t n = points.size();
            const std::size_t count = closed ? n : n - 1;
            std::vector<Vec2> directions(count);
            for (std::size_t i = 0; i < count; ++i)
            {
                const Vec2 d = points[(i + 1) % n] - points[i];
                directions[i] = d / Length(d);
            }
            return directions;
        }

        void StrokeOpen(std::vector<Contour>& out, const std::vector<Vec2>& points, float halfWidth,
                        const StrokeStyle& style, float tolerance)
        {
            const std::vector<Vec2> d = Directions(points, false);
            std::vector<Vec2> left;
            std::vector<Vec2> right;
            AddPoint(left, points.front() + Left(d.front()) * halfWidth);
            AddPoint(right, points.front() + Right(d.front()) * halfWidth);
            for (std::size_t i = 1; i + 1 < points.size(); ++i)
            {
                Join(left, right, points[i], d[i - 1], d[i], halfWidth, style, tolerance);
            }
            AddPoint(left, points.back() + Left(d.back()) * halfWidth);
            AddPoint(right, points.back() + Right(d.back()) * halfWidth);

            Contour outline;
            outline.closed = true;
            outline.points = left;
            Cap(outline.points, points.back(), d.back(), halfWidth, style.cap, tolerance);
            for (auto it = right.rbegin(); it != right.rend(); ++it)
            {
                AddPoint(outline.points, *it);
            }
            Cap(outline.points, points.front(), -d.front(), halfWidth, style.cap, tolerance);
            out.push_back(std::move(outline));
        }

        void StrokeClosed(std::vector<Contour>& out, const std::vector<Vec2>& points, float halfWidth,
                          const StrokeStyle& style, float tolerance)
        {
            const std::vector<Vec2> d = Directions(points, true);
            const std::size_t n = points.size();
            Contour left;
            Contour right;
            left.closed = true;
            right.closed = true;
            for (std::size_t i = 0; i < n; ++i)
            {
                Join(left.points, right.points, points[i], d[(i + n - 1) % n], d[i], halfWidth, style, tolerance);
            }
            std::reverse(right.points.begin(), right.points.end());
            out.push_back(std::move(left));
            out.push_back(std::move(right));
        }

        /** @brief `contours` cut into their dashes, which are open; a closed contour's dash over its start is one. */
        std::vector<Contour> Dash(const std::vector<Contour>& contours, const StrokeStyle& style)
        {
            std::vector<float> pattern;
            for (const float length : style.dashes)
            {
                pattern.push_back(std::max(length, 0.0f));
            }
            if (pattern.size() % 2 == 1)
            {
                pattern.insert(pattern.end(), pattern.begin(), pattern.end());
            }
            const float total = std::accumulate(pattern.begin(), pattern.end(), 0.0f);
            if (pattern.empty() || !(total > 0.0f))
            {
                return contours;
            }

            std::vector<Contour> dashes;
            for (const Contour& contour : contours)
            {
                std::vector<Vec2> points = contour.points;
                if (contour.closed && points.size() > 1)
                {
                    points.push_back(points.front());
                }
                // Where in the pattern the contour starts.
                float offset = std::fmod(style.dashOffset, total);
                if (offset < 0.0f)
                {
                    offset += total;
                }
                std::size_t index = 0;
                float remaining = pattern[0];
                while (offset > 0.0f)
                {
                    if (offset >= remaining)
                    {
                        offset -= remaining;
                        index = (index + 1) % pattern.size();
                        remaining = pattern[index];
                    }
                    else
                    {
                        remaining -= offset;
                        offset = 0.0f;
                    }
                }

                const bool startsOn = index % 2 == 0;
                bool on = startsOn;
                bool toggled = false;
                const std::size_t first = dashes.size();
                Contour piece;
                if (on)
                {
                    piece.points.push_back(points.front());
                }
                for (std::size_t i = 0; i + 1 < points.size(); ++i)
                {
                    const Vec2 a = points[i];
                    const Vec2 b = points[i + 1];
                    const float length = Length(b - a);
                    float position = 0.0f;
                    while (length - position > remaining)
                    {
                        position += remaining;
                        const Vec2 point = a + (b - a) * (position / length);
                        if (on)
                        {
                            AddPoint(piece.points, point);
                            if (piece.points.size() == 1 && style.cap != LineCap::Butt)
                            {
                                // A dash of no length is a dot of its caps, as SVG draws it: a step too short to see
                                // gives the caps their direction.
                                piece.points.push_back(point + (b - a) * (DotLength / length));
                            }
                            if (piece.points.size() > 1)
                            {
                                dashes.push_back(std::move(piece));
                            }
                            piece = {};
                        }
                        else
                        {
                            piece.points = {point};
                        }
                        on = !on;
                        toggled = true;
                        index = (index + 1) % pattern.size();
                        remaining = pattern[index];
                    }
                    remaining -= length - position;
                    if (on)
                    {
                        AddPoint(piece.points, b);
                    }
                }

                if (!toggled && on)
                {
                    // One dash covers it all: it is the contour, still closed if it was.
                    dashes.push_back(contour);
                    continue;
                }
                const bool endsOn = on && piece.points.size() > 1;
                if (endsOn)
                {
                    dashes.push_back(std::move(piece));
                }
                if (contour.closed && startsOn && endsOn && dashes.size() - first >= 2)
                {
                    // The last dash runs on through the start into the first.
                    Contour& last = dashes.back();
                    Contour& head = dashes[first];
                    for (std::size_t k = 1; k < head.points.size(); ++k)
                    {
                        AddPoint(last.points, head.points[k]);
                    }
                    head = std::move(last);
                    dashes.pop_back();
                }
            }
            return dashes;
        }

        // --- Fills -----------------------------------------------------------------------------------------------

        struct Segment
        {
            Vec2 a;
            Vec2 b;
        };

        /** @brief An edge between two of the graph's vertices, `count` times from the lower index to the higher. */
        struct Edge
        {
            std::uint32_t a = 0;
            std::uint32_t b = 0;
            int count = 0;
        };

        /** @brief The fill's vertices, snapped to the grid and shared by every edge that reaches them. */
        class Vertices
        {
          public:
            [[nodiscard]] static Vec2 Snap(Vec2 p)
            {
                return {std::round(p.x * Grid) / Grid, std::round(p.y * Grid) / Grid};
            }

            std::uint32_t Add(Vec2 p)
            {
                const Vec2 snapped = Snap(p);
                const auto x = static_cast<std::int64_t>(std::llround(snapped.x * Grid));
                const auto y = static_cast<std::int64_t>(std::llround(snapped.y * Grid));
                const std::uint64_t key = (static_cast<std::uint64_t>(x) << 32u) ^ static_cast<std::uint32_t>(y);
                const auto [it, added] = m_index.try_emplace(key, static_cast<std::uint32_t>(positions.size()));
                if (added)
                {
                    positions.push_back(snapped);
                }
                return it->second;
            }

            std::vector<Vec2> positions;

          private:
            std::unordered_map<std::uint64_t, std::uint32_t> m_index;
        };

        /** @brief Adds to `splits` where segments `i` and `j` meet, as fractions along each, their ends excluded. */
        void Intersect(const std::vector<Segment>& segments, std::size_t i, std::size_t j,
                       std::vector<std::vector<float>>& splits)
        {
            constexpr float Eps = 1e-6f;
            const Vec2 p = segments[i].a;
            const Vec2 r = segments[i].b - p;
            const Vec2 q = segments[j].a;
            const Vec2 s = segments[j].b - q;
            const Vec2 qp = q - p;
            const float rLength = Length(r);
            const float sLength = Length(s);
            const float denominator = Cross(r, s);
            const auto add = [&](std::size_t segment, float t) {
                if (t > Eps && t < 1.0f - Eps)
                {
                    splits[segment].push_back(t);
                }
            };
            if (std::abs(denominator) > 1e-7f * rLength * sLength)
            {
                const float t = Cross(qp, s) / denominator;
                const float u = Cross(qp, r) / denominator;
                if (t >= -Eps && t <= 1.0f + Eps && u >= -Eps && u <= 1.0f + Eps)
                {
                    add(i, t);
                    add(j, u);
                }
                return;
            }
            // Parallel: only collinear overlapping ones meet, at each other's ends.
            if (std::abs(Cross(qp, r)) > 1e-4f * rLength)
            {
                return;
            }
            const float rr = Dot(r, r);
            const float ss = Dot(s, s);
            add(i, Dot(qp, r) / rr);
            add(i, Dot(qp + s, r) / rr);
            add(j, Dot(p - q, s) / ss);
            add(j, Dot(p + r - q, s) / ss);
        }

        /**
         * @brief The edges sorted into bands across Y (`acrossY`) or X, each band listing the edges reaching into it,
         * so a ray along the other axis tests only the edges of its own band.
         */
        class Bands
        {
          public:
            Bands(const std::vector<Edge>& edges, const std::vector<Vec2>& positions, bool acrossY) : m_acrossY(acrossY)
            {
                if (edges.empty())
                {
                    return;
                }
                const auto coordinate = [acrossY](Vec2 p) { return acrossY ? p.y : p.x; };
                m_low = std::numeric_limits<float>::infinity();
                float high = -m_low;
                for (const Vec2 p : positions)
                {
                    m_low = std::min(m_low, coordinate(p));
                    high = std::max(high, coordinate(p));
                }
                const auto count =
                    static_cast<std::size_t>(std::clamp(std::sqrt(static_cast<float>(edges.size())), 1.0f, 4096.0f));
                m_scale = high > m_low ? static_cast<float>(count) / (high - m_low) : 0.0f;
                m_bands.resize(count);
                for (std::size_t e = 0; e < edges.size(); ++e)
                {
                    const float a = coordinate(positions[edges[e].a]);
                    const float b = coordinate(positions[edges[e].b]);
                    for (std::size_t band = Band(std::min(a, b)); band <= Band(std::max(a, b)); ++band)
                    {
                        m_bands[band].push_back(e);
                    }
                }
            }

            /** @brief The edges that may reach `p`'s coordinate across the bands. */
            [[nodiscard]] const std::vector<std::size_t>& At(Vec2 p) const
            {
                static const std::vector<std::size_t> none;
                return m_bands.empty() ? none : m_bands[Band(m_acrossY ? p.y : p.x)];
            }

          private:
            [[nodiscard]] std::size_t Band(float value) const
            {
                const float band = std::floor((value - m_low) * m_scale);
                return std::min(static_cast<std::size_t>(std::max(band, 0.0f)), m_bands.size() - 1);
            }

            bool m_acrossY = true;
            float m_low = 0.0f;
            float m_scale = 0.0f;
            std::vector<std::vector<std::size_t>> m_bands;
        };

        /**
         * @brief The winding number at `q` by a ray along +X (`alongX`) or +Y, every edge but `skip` counted where it
         * crosses: +X crossings by their direction in Y, +Y crossings by theirs in X. `bands` are across Y for a ray
         * along X, across X for one along Y.
         */
        int Winding(const std::vector<Edge>& edges, const std::vector<Vec2>& positions, const Bands& bands, Vec2 q,
                    std::size_t skip, bool alongX)
        {
            int winding = 0;
            for (const std::size_t e : bands.At(q))
            {
                if (e == skip)
                {
                    continue;
                }
                const Vec2 a = positions[edges[e].a];
                const Vec2 b = positions[edges[e].b];
                if (alongX)
                {
                    if ((a.y > q.y) != (b.y > q.y))
                    {
                        const float x = a.x + (q.y - a.y) * (b.x - a.x) / (b.y - a.y);
                        if (x > q.x)
                        {
                            winding += edges[e].count * (b.y > a.y ? 1 : -1);
                        }
                    }
                }
                else if ((a.x > q.x) != (b.x > q.x))
                {
                    const float y = a.y + (q.x - a.x) * (b.y - a.y) / (b.x - a.x);
                    if (y > q.y)
                    {
                        winding += edges[e].count * (b.x > a.x ? 1 : -1);
                    }
                }
            }
            return winding;
        }

        bool Inside(int winding, FillRule rule)
        {
            return rule == FillRule::NonZero ? winding != 0 : winding % 2 != 0;
        }

        /** @brief Twice the signed area of the loop: positive when it keeps what it covers on its left. */
        float SignedArea(const std::vector<std::uint32_t>& loop, const std::vector<Vec2>& positions)
        {
            float area = 0.0f;
            for (std::size_t i = 0; i < loop.size(); ++i)
            {
                area += Cross(positions[loop[i]], positions[loop[(i + 1) % loop.size()]]);
            }
            return area;
        }

        bool PointInLoop(Vec2 q, const std::vector<std::uint32_t>& loop, const std::vector<Vec2>& positions)
        {
            bool inside = false;
            for (std::size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++)
            {
                const Vec2 a = positions[loop[i]];
                const Vec2 b = positions[loop[j]];
                if ((a.y > q.y) != (b.y > q.y) && q.x < a.x + (q.y - a.y) * (b.x - a.x) / (b.y - a.y))
                {
                    inside = !inside;
                }
            }
            return inside;
        }

        /** @brief Removes the loop's vertices that lie in a straight run between their neighbours. */
        void DropStraights(std::vector<std::uint32_t>& loop, const std::vector<Vec2>& positions)
        {
            bool changed = true;
            while (changed && loop.size() > 3)
            {
                changed = false;
                for (std::size_t i = 0; i < loop.size() && loop.size() > 3;)
                {
                    const Vec2 a = positions[loop[(i + loop.size() - 1) % loop.size()]];
                    const Vec2 b = positions[loop[i]];
                    const Vec2 c = positions[loop[(i + 1) % loop.size()]];
                    const Vec2 in = b - a;
                    const Vec2 out = c - b;
                    if (std::abs(Cross(in, out)) <= 1e-6f * Length(in) * Length(out) && Dot(in, out) > 0.0f)
                    {
                        loop.erase(loop.begin() + static_cast<std::ptrdiff_t>(i));
                        changed = true;
                    }
                    else
                    {
                        ++i;
                    }
                }
            }
        }

        /**
         * @brief The region's boundary as loops that cross nowhere, each keeping what it covers on its left: the
         * contours' segments split where they meet, each edge kept where its sides differ under the rule.
         */
        std::vector<std::vector<std::uint32_t>> Boundary(const std::vector<Contour>& contours, FillRule rule,
                                                         std::vector<Vec2>& positions)
        {
            std::vector<Segment> segments;
            for (const Contour& contour : contours)
            {
                const std::size_t n = contour.points.size();
                for (std::size_t i = 0; n >= 2 && i < n; ++i)
                {
                    const Segment segment{Vertices::Snap(contour.points[i]),
                                          Vertices::Snap(contour.points[(i + 1) % n])};
                    if (segment.a != segment.b)
                    {
                        segments.push_back(segment);
                    }
                }
            }

            // Where segments meet, swept along X so only those whose extents overlap are tested.
            std::vector<std::vector<float>> splits(segments.size());
            std::vector<std::size_t> order(segments.size());
            std::iota(order.begin(), order.end(), 0);
            const auto minX = [&](std::size_t i) { return std::min(segments[i].a.x, segments[i].b.x); };
            const auto maxX = [&](std::size_t i) { return std::max(segments[i].a.x, segments[i].b.x); };
            std::ranges::sort(order, {}, minX);
            for (std::size_t m = 0; m < order.size(); ++m)
            {
                const std::size_t i = order[m];
                const float iMinY = std::min(segments[i].a.y, segments[i].b.y);
                const float iMaxY = std::max(segments[i].a.y, segments[i].b.y);
                for (std::size_t k = m + 1; k < order.size() && minX(order[k]) <= maxX(i); ++k)
                {
                    const std::size_t j = order[k];
                    if (std::max(segments[j].a.y, segments[j].b.y) < iMinY ||
                        std::min(segments[j].a.y, segments[j].b.y) > iMaxY)
                    {
                        continue;
                    }
                    Intersect(segments, i, j, splits);
                }
            }

            // The split segments as edges between shared vertices, opposite runs cancelling.
            Vertices vertices;
            std::unordered_map<std::uint64_t, int> counts;
            std::vector<std::uint64_t> keys;
            for (std::size_t i = 0; i < segments.size(); ++i)
            {
                std::vector<float>& ts = splits[i];
                std::ranges::sort(ts);
                ts.push_back(1.0f);
                std::uint32_t from = vertices.Add(segments[i].a);
                for (const float t : ts)
                {
                    const std::uint32_t to = t >= 1.0f
                                                 ? vertices.Add(segments[i].b)
                                                 : vertices.Add(segments[i].a + (segments[i].b - segments[i].a) * t);
                    if (to != from)
                    {
                        const std::uint64_t key =
                            (static_cast<std::uint64_t>(std::min(from, to)) << 32u) | std::max(from, to);
                        const auto [it, added] = counts.try_emplace(key, 0);
                        if (added)
                        {
                            keys.push_back(key);
                        }
                        it->second += from < to ? 1 : -1;
                    }
                    from = to;
                }
            }
            positions = std::move(vertices.positions);
            std::vector<Edge> edges;
            for (const std::uint64_t key : keys)
            {
                if (const int count = counts[key]; count != 0)
                {
                    edges.push_back({static_cast<std::uint32_t>(key >> 32u), static_cast<std::uint32_t>(key), count});
                }
            }

            // Each edge's sides by the winding just beside its middle, kept and turned so what is covered is on its
            // left when only one side is.
            std::vector<std::pair<std::uint32_t, std::uint32_t>> boundary;
            const Bands rows(edges, positions, true);
            const Bands columns(edges, positions, false);
            for (std::size_t e = 0; e < edges.size(); ++e)
            {
                const Vec2 a = positions[edges[e].a];
                const Vec2 b = positions[edges[e].b];
                const Vec2 d = b - a;
                const Vec2 middle = (a + b) * 0.5f;
                const bool alongX = std::abs(d.y) >= std::abs(d.x);
                const int beyond = Winding(edges, positions, alongX ? rows : columns, middle, e, alongX);
                // Beside the edge on the side the ray starts from, the ray also crosses the edge itself.
                const int before = beyond + edges[e].count * ((alongX ? d.y : d.x) > 0.0f ? 1 : -1);
                const bool coveredBefore = Inside(before, rule);
                if (coveredBefore == Inside(beyond, rule))
                {
                    continue;
                }
                // Before is -X (left of an edge heading +Y) or -Y (left of an edge heading -X).
                const bool keep = alongX ? coveredBefore == (d.y > 0.0f) : coveredBefore == (d.x < 0.0f);
                boundary.emplace_back(keep ? edges[e].a : edges[e].b, keep ? edges[e].b : edges[e].a);
            }

            // Walked into loops, at each vertex taking the sharpest left turn so touching loops stay apart.
            std::vector<std::vector<std::size_t>> outgoing(positions.size());
            for (std::size_t e = 0; e < boundary.size(); ++e)
            {
                outgoing[boundary[e].first].push_back(e);
            }
            std::vector<bool> used(boundary.size(), false);
            std::vector<std::vector<std::uint32_t>> loops;
            for (std::size_t start = 0; start < boundary.size(); ++start)
            {
                if (used[start])
                {
                    continue;
                }
                std::vector<std::uint32_t> loop;
                std::size_t current = start;
                bool closed = false;
                while (true)
                {
                    used[current] = true;
                    loop.push_back(boundary[current].first);
                    const std::uint32_t at = boundary[current].second;
                    if (at == boundary[start].first)
                    {
                        closed = true;
                        break;
                    }
                    const Vec2 in = positions[at] - positions[boundary[current].first];
                    std::size_t next = boundary.size();
                    float best = -std::numeric_limits<float>::infinity();
                    for (const std::size_t candidate : outgoing[at])
                    {
                        if (used[candidate])
                        {
                            continue;
                        }
                        const Vec2 out = positions[boundary[candidate].second] - positions[at];
                        const float turn = std::atan2(Cross(in, out), Dot(in, out));
                        if (turn > best)
                        {
                            best = turn;
                            next = candidate;
                        }
                    }
                    if (next == boundary.size())
                    {
                        break;
                    }
                    current = next;
                }
                if (closed)
                {
                    DropStraights(loop, positions);
                }
                if (closed && loop.size() >= 3 && std::abs(SignedArea(loop, positions)) > 1e-8f)
                {
                    loops.push_back(std::move(loop));
                }
            }
            return loops;
        }

        /** @brief The angle of `v` in [0, 2 pi). */
        float Angle(Vec2 v)
        {
            const float angle = std::atan2(v.y, v.x);
            return angle < 0.0f ? angle + TwoPi : angle;
        }

        /** @brief Whether heading `towards` from the corner at `at` between `previous` and `next` goes inside. */
        bool LocallyInside(Vec2 previous, Vec2 at, Vec2 next, Vec2 towards)
        {
            const float outAngle = Angle(next - at);
            const auto from = [outAngle](float angle) {
                const float turned = angle - outAngle;
                return turned < 0.0f ? turned + TwoPi : turned;
            };
            const float span = from(Angle(previous - at));
            const float direction = from(Angle(towards));
            return direction > 0.0f && direction < span;
        }

        /** @brief Whether `p`, on the line through `a` and `b`, lies between them. */
        bool Between(Vec2 a, Vec2 b, Vec2 p)
        {
            return p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x) && p.y >= std::min(a.y, b.y) &&
                   p.y <= std::max(a.y, b.y);
        }

        /** @brief Whether segments `ab` and `cd` cross or touch other than at an end they share. */
        bool Blocks(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
        {
            if (a == c || a == d || b == c || b == d)
            {
                return false;
            }
            const float o1 = Cross(b - a, c - a);
            const float o2 = Cross(b - a, d - a);
            const float o3 = Cross(d - c, a - c);
            const float o4 = Cross(d - c, b - c);
            if (((o1 > 0.0f && o2 < 0.0f) || (o1 < 0.0f && o2 > 0.0f)) &&
                ((o3 > 0.0f && o4 < 0.0f) || (o3 < 0.0f && o4 > 0.0f)))
            {
                return true;
            }
            return (o1 == 0.0f && Between(a, b, c)) || (o2 == 0.0f && Between(a, b, d)) ||
                   (o3 == 0.0f && Between(c, d, a)) || (o4 == 0.0f && Between(c, d, b));
        }

        /**
         * @brief `hole` joined into `polygon` by a bridge from the hole's rightmost vertex to the nearest polygon
         * vertex it can see, both crossed twice; `others` are the holes still to join, which the bridge must not cross.
         */
        void Bridge(std::vector<std::uint32_t>& polygon, const std::vector<std::uint32_t>& hole,
                    const std::vector<const std::vector<std::uint32_t>*>& others, const std::vector<Vec2>& positions)
        {
            std::size_t m = 0;
            for (std::size_t i = 1; i < hole.size(); ++i)
            {
                if (positions[hole[i]].x > positions[hole[m]].x)
                {
                    m = i;
                }
            }
            const Vec2 h = positions[hole[m]];
            const auto crossesLoop = [&](Vec2 a, Vec2 b, const std::vector<std::uint32_t>& loop) {
                for (std::size_t i = 0; i < loop.size(); ++i)
                {
                    if (Blocks(a, b, positions[loop[i]], positions[loop[(i + 1) % loop.size()]]))
                    {
                        return true;
                    }
                }
                return false;
            };
            std::vector<std::size_t> candidates(polygon.size());
            std::iota(candidates.begin(), candidates.end(), 0);
            std::ranges::sort(candidates, {}, [&](std::size_t k) {
                const Vec2 d = positions[polygon[k]] - h;
                return Dot(d, d);
            });
            std::size_t chosen = candidates.front();
            for (const std::size_t k : candidates)
            {
                const Vec2 v = positions[polygon[k]];
                if (v == h)
                {
                    chosen = k;
                    break;
                }
                const std::size_t n = polygon.size();
                const bool visible =
                    LocallyInside(positions[polygon[(k + n - 1) % n]], v, positions[polygon[(k + 1) % n]], h - v) &&
                    LocallyInside(positions[hole[(m + hole.size() - 1) % hole.size()]], h,
                                  positions[hole[(m + 1) % hole.size()]], v - h) &&
                    !crossesLoop(h, v, polygon) && !crossesLoop(h, v, hole) &&
                    std::ranges::none_of(others, [&](const auto* other) { return crossesLoop(h, v, *other); });
                if (visible)
                {
                    chosen = k;
                    break;
                }
            }
            std::vector<std::uint32_t> joined;
            joined.reserve(polygon.size() + hole.size() + 2);
            joined.insert(joined.end(), polygon.begin(), polygon.begin() + static_cast<std::ptrdiff_t>(chosen) + 1);
            for (std::size_t i = 0; i <= hole.size(); ++i)
            {
                joined.push_back(hole[(m + i) % hole.size()]);
            }
            joined.insert(joined.end(), polygon.begin() + static_cast<std::ptrdiff_t>(chosen), polygon.end());
            polygon = std::move(joined);
        }

        bool InTriangle(Vec2 a, Vec2 b, Vec2 c, Vec2 p)
        {
            return Cross(b - a, p - a) >= 0.0f && Cross(c - b, p - b) >= 0.0f && Cross(a - c, p - c) >= 0.0f;
        }

        /** @brief Appends `polygon`'s triangles (it keeps its inside on the left) to `indices`, by ear clipping. */
        void EarClip(const std::vector<std::uint32_t>& polygon, const std::vector<Vec2>& positions,
                     std::vector<std::uint32_t>& indices)
        {
            const std::size_t n = polygon.size();
            std::vector<std::size_t> previous(n);
            std::vector<std::size_t> next(n);
            for (std::size_t i = 0; i < n; ++i)
            {
                previous[i] = (i + n - 1) % n;
                next[i] = (i + 1) % n;
            }
            const auto at = [&](std::size_t i) { return positions[polygon[i]]; };
            const auto turn = [&](std::size_t i) { return Cross(at(i) - at(previous[i]), at(next[i]) - at(i)); };
            const auto flat = [&](std::size_t i) {
                const float scale = Length(at(i) - at(previous[i])) * Length(at(next[i]) - at(i));
                return std::abs(turn(i)) <= 1e-6f * scale;
            };
            const auto isEar = [&](std::size_t i) {
                const Vec2 a = at(previous[i]);
                const Vec2 b = at(i);
                const Vec2 c = at(next[i]);
                // Only a reflex vertex can lie inside a convex corner's triangle.
                for (std::size_t k = next[next[i]]; k != previous[i]; k = next[k])
                {
                    const Vec2 p = at(k);
                    if (p == a || p == b || p == c || turn(k) > 0.0f)
                    {
                        continue;
                    }
                    if (InTriangle(a, b, c, p))
                    {
                        return false;
                    }
                }
                return true;
            };
            const auto clip = [&](std::size_t i, bool emit) {
                if (emit)
                {
                    indices.push_back(polygon[previous[i]]);
                    indices.push_back(polygon[i]);
                    indices.push_back(polygon[next[i]]);
                }
                next[previous[i]] = next[i];
                previous[next[i]] = previous[i];
            };

            std::size_t remaining = n;
            std::size_t i = 0;
            std::size_t stalled = 0;
            while (remaining > 3)
            {
                const std::size_t following = next[i];
                if (flat(i))
                {
                    // A vertex in a straight run, or the tip of a spike of no area: nothing to fill.
                    clip(i, false);
                }
                else if (turn(i) > 0.0f && isEar(i))
                {
                    clip(i, true);
                }
                else if (++stalled > remaining)
                {
                    // No ear left, which rounding can cause: clip this corner anyway so the loop ends.
                    clip(i, turn(i) > 0.0f);
                }
                else
                {
                    i = following;
                    continue;
                }
                --remaining;
                stalled = 0;
                i = following;
            }
            if (remaining == 3 && !flat(i) && turn(i) > 0.0f)
            {
                clip(i, true);
            }
        }
    } // namespace

    std::vector<Contour> Flatten(const Path& path, const Transform2D& transform, float tolerance)
    {
        std::vector<Contour> contours;
        Contour current;
        const auto finish = [&]() {
            if (current.closed && current.points.size() > 1)
            {
                const Vec2 d = current.points.back() - current.points.front();
                if (Dot(d, d) <= 1e-10f)
                {
                    current.points.pop_back();
                }
            }
            if (current.points.size() > 1)
            {
                contours.push_back(std::move(current));
            }
            current = {};
        };

        const std::span<const Vec2> points = path.Points();
        std::size_t next = 0;
        Vec2 last{0.0f};
        for (const PathVerb verb : path.Verbs())
        {
            switch (verb)
            {
            case PathVerb::Move:
                finish();
                last = transform.Apply(points[next++]);
                current.points.push_back(last);
                break;
            case PathVerb::Line:
                last = transform.Apply(points[next++]);
                AddPoint(current.points, last);
                break;
            case PathVerb::Quad: {
                const Vec2 control = transform.Apply(points[next++]);
                const Vec2 end = transform.Apply(points[next++]);
                // Split into n lines, a quadratic strays |p0 - 2 p1 + p2| / (4 n^2) from them.
                const int pieces = Pieces(Length(last - control * 2.0f + end) / 4.0f, tolerance);
                for (int k = 1; k <= pieces; ++k)
                {
                    const float t = static_cast<float>(k) / static_cast<float>(pieces);
                    const float u = 1.0f - t;
                    AddPoint(current.points,
                             k == pieces ? end : last * (u * u) + control * (2.0f * u * t) + end * (t * t));
                }
                last = end;
                break;
            }
            case PathVerb::Cubic: {
                const Vec2 c1 = transform.Apply(points[next++]);
                const Vec2 c2 = transform.Apply(points[next++]);
                const Vec2 end = transform.Apply(points[next++]);
                // A cubic's second derivative is at most 6 times its larger second difference; over a line of
                // parameter 1 / n it strays an eighth of that over n^2.
                const float second = std::max(Length(last - c1 * 2.0f + c2), Length(c1 - c2 * 2.0f + end));
                const int pieces = Pieces(0.75f * second, tolerance);
                for (int k = 1; k <= pieces; ++k)
                {
                    const float t = static_cast<float>(k) / static_cast<float>(pieces);
                    const float u = 1.0f - t;
                    AddPoint(current.points, k == pieces ? end
                                                         : last * (u * u * u) + c1 * (3.0f * u * u * t) +
                                                               c2 * (3.0f * u * t * t) + end * (t * t * t));
                }
                last = end;
                break;
            }
            case PathVerb::Close:
                current.closed = true;
                finish();
                break;
            }
        }
        finish();
        return contours;
    }

    std::vector<Contour> StrokeContours(const std::vector<Contour>& contours, const StrokeStyle& style, float tolerance)
    {
        const float halfWidth = style.width * 0.5f;
        std::vector<Contour> outline;
        if (!(halfWidth > 0.0f))
        {
            return outline;
        }
        for (const Contour& contour : Dash(contours, style))
        {
            if (contour.points.size() < 2)
            {
                continue;
            }
            if (contour.closed)
            {
                StrokeClosed(outline, contour.points, halfWidth, style, tolerance);
            }
            else
            {
                StrokeOpen(outline, contour.points, halfWidth, style, tolerance);
            }
        }
        return outline;
    }

    Tessellation Tessellate(const std::vector<Contour>& contours, FillRule rule, float fringe)
    {
        std::vector<Vec2> positions;
        std::vector<std::vector<std::uint32_t>> loops = Boundary(contours, rule, positions);

        // Outer loops wind positively, holes negatively; each hole belongs to the smallest outer loop round it.
        std::vector<std::size_t> outers;
        std::vector<float> areas(loops.size());
        for (std::size_t i = 0; i < loops.size(); ++i)
        {
            areas[i] = SignedArea(loops[i], positions);
            if (areas[i] > 0.0f)
            {
                outers.push_back(i);
            }
        }
        std::vector<std::vector<std::size_t>> holes(loops.size());
        for (std::size_t i = 0; i < loops.size(); ++i)
        {
            if (areas[i] > 0.0f)
            {
                continue;
            }
            // Just beside the hole's first edge, on the covered side.
            const Vec2 a = positions[loops[i][0]];
            const Vec2 b = positions[loops[i][1]];
            const Vec2 probe = (a + b) * 0.5f + Left(b - a) * (std::min(1e-3f / std::max(Length(b - a), 1e-6f), 0.25f));
            std::size_t owner = loops.size();
            for (const std::size_t outer : outers)
            {
                if (PointInLoop(probe, loops[outer], positions) &&
                    (owner == loops.size() || areas[outer] < areas[owner]))
                {
                    owner = outer;
                }
            }
            if (owner != loops.size())
            {
                holes[owner].push_back(i);
            }
        }

        // Every loop's vertices, each in two copies: half the fringe in and half out, moved along the corner's miter.
        // With no fringe, each is one copy where it is.
        const bool fringed = fringe > 0.0f;
        const std::uint32_t copies = fringed ? 2 : 1;
        Tessellation result;
        std::vector<std::uint32_t> first(loops.size());
        for (std::size_t l = 0; l < loops.size(); ++l)
        {
            const std::vector<std::uint32_t>& loop = loops[l];
            first[l] = static_cast<std::uint32_t>(result.positions.size() / copies);
            const std::size_t n = loop.size();
            for (std::size_t i = 0; i < n; ++i)
            {
                const Vec2 p = positions[loop[i]];
                if (!fringed)
                {
                    result.positions.push_back(p);
                    result.coverage.push_back(1.0f);
                    continue;
                }
                const Vec2 n0 = Math::Normalized(Right(p - positions[loop[(i + n - 1) % n]]));
                const Vec2 n1 = Math::Normalized(Right(positions[loop[(i + 1) % n]] - p));
                Vec2 miter = (n0 + n1) * 0.5f;
                const float length2 = Dot(miter, miter);
                if (length2 > 1e-6f)
                {
                    miter *= std::min(1.0f / length2, MaxMiter * MaxMiter);
                }
                result.positions.push_back(p - miter * (fringe * 0.5f));
                result.coverage.push_back(1.0f);
                result.positions.push_back(p + miter * (fringe * 0.5f));
                result.coverage.push_back(0.0f);
            }
        }
        // Loop vertex references, for the ear clipping, as the inner copies' indices.
        const auto reference = [&](std::size_t l, std::size_t i) { return first[l] + static_cast<std::uint32_t>(i); };
        std::vector<Vec2> loopPositions(result.positions.size() / copies);
        for (std::size_t l = 0; l < loops.size(); ++l)
        {
            for (std::size_t i = 0; i < loops[l].size(); ++i)
            {
                loopPositions[reference(l, i)] = positions[loops[l][i]];
            }
        }

        std::vector<std::uint32_t> solid;
        for (const std::size_t outer : outers)
        {
            const auto references = [&](std::size_t l) {
                std::vector<std::uint32_t> refs(loops[l].size());
                for (std::size_t i = 0; i < refs.size(); ++i)
                {
                    refs[i] = reference(l, i);
                }
                return refs;
            };
            std::vector<std::uint32_t> polygon = references(outer);
            std::vector<std::vector<std::uint32_t>> holeRefs;
            for (const std::size_t hole : holes[outer])
            {
                holeRefs.push_back(references(hole));
            }
            // Rightmost holes first, so each bridge runs right to vertices already joined.
            std::ranges::sort(holeRefs, std::greater<>{}, [&](const std::vector<std::uint32_t>& hole) {
                float most = -std::numeric_limits<float>::infinity();
                for (const std::uint32_t r : hole)
                {
                    most = std::max(most, loopPositions[r].x);
                }
                return most;
            });
            for (std::size_t h = 0; h < holeRefs.size(); ++h)
            {
                std::vector<const std::vector<std::uint32_t>*> others;
                for (std::size_t k = h + 1; k < holeRefs.size(); ++k)
                {
                    others.push_back(&holeRefs[k]);
                }
                Bridge(polygon, holeRefs[h], others, loopPositions);
            }
            EarClip(polygon, loopPositions, solid);
        }

        for (const std::uint32_t r : solid)
        {
            result.indices.push_back(r * copies);
        }
        if (!fringed)
        {
            return result;
        }
        for (std::size_t l = 0; l < loops.size(); ++l)
        {
            const std::size_t n = loops[l].size();
            for (std::size_t i = 0; i < n; ++i)
            {
                const std::uint32_t a = reference(l, i) * 2;
                const std::uint32_t b = reference(l, (i + 1) % n) * 2;
                result.indices.insert(result.indices.end(), {a, a + 1, b + 1, a, b + 1, b});
            }
        }
        return result;
    }
} // namespace Fenrir::PainterDetail
