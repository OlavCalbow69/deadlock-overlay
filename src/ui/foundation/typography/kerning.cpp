#include "ui/foundation/typography/kerning.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <unordered_map>
#include <utility>

namespace solace::kerning
{
namespace
{

struct reader
{
    const unsigned char* p = nullptr;
    size_t n = 0;

    bool ok(size_t at, size_t len) const
    {
        return at <= n && len <= n - at;
    }
    unsigned int u16(size_t at) const
    {
        return ok(at, 2) ? (unsigned int)((p[at] << 8) | p[at + 1]) : 0u;
    }
    int s16(size_t at) const
    {
        const int v = (int)u16(at);
        return v >= 0x8000 ? v - 0x10000 : v;
    }
    unsigned int u32(size_t at) const
    {
        return ok(at, 4) ? (((unsigned int)p[at] << 24) | ((unsigned int)p[at + 1] << 16) |
                            ((unsigned int)p[at + 2] << 8) | (unsigned int)p[at + 3])
                         : 0u;
    }
};

struct coverage
{

    std::unordered_map<unsigned int, unsigned int> index;

    void parse(const reader& r, size_t at)
    {
        const unsigned int format = r.u16(at);
        if (format == 1)
        {
            const unsigned int count = r.u16(at + 2);
            for (unsigned int i = 0; i < count; i++)
                index[r.u16(at + 4 + i * 2)] = i;
        }
        else if (format == 2)
        {
            const unsigned int ranges = r.u16(at + 2);
            for (unsigned int i = 0; i < ranges; i++)
            {
                const size_t rec = at + 4 + i * 6;
                const unsigned int start = r.u16(rec);
                const unsigned int end = r.u16(rec + 2);
                const unsigned int base = r.u16(rec + 4);
                for (unsigned int g = start; g <= end && g - start < 0xFFFF; g++)
                    index[g] = base + (g - start);
            }
        }
    }

    int find(unsigned int glyph) const
    {
        auto it = index.find(glyph);
        return it == index.end() ? -1 : (int)it->second;
    }
};

struct class_def
{
    std::unordered_map<unsigned int, unsigned int> classes;

    void parse(const reader& r, size_t at)
    {
        const unsigned int format = r.u16(at);
        if (format == 1)
        {
            const unsigned int start = r.u16(at + 2);
            const unsigned int count = r.u16(at + 4);
            for (unsigned int i = 0; i < count; i++)
            {
                const unsigned int c = r.u16(at + 6 + i * 2);
                if (c != 0)
                    classes[start + i] = c;
            }
        }
        else if (format == 2)
        {
            const unsigned int ranges = r.u16(at + 2);
            for (unsigned int i = 0; i < ranges; i++)
            {
                const size_t rec = at + 4 + i * 6;
                const unsigned int start = r.u16(rec);
                const unsigned int end = r.u16(rec + 2);
                const unsigned int c = r.u16(rec + 4);
                if (c == 0)
                    continue;
                for (unsigned int g = start; g <= end && g - start < 0xFFFF; g++)
                    classes[g] = c;
            }
        }
    }

    unsigned int of(unsigned int glyph) const
    {
        auto it = classes.find(glyph);
        return it == classes.end() ? 0u : it->second;
    }
};

struct pair_format2
{
    coverage cov;
    class_def class1, class2;
    unsigned int class1_count = 0, class2_count = 0;
    std::vector<float> values;
};

int value_record_size(unsigned int format)
{
    int size = 0;
    for (int bit = 0; bit < 8; bit++)
        if (format & (1u << bit))
            size += 2;
    return size;
}

int x_advance_offset(unsigned int format)
{
    if ((format & 0x0004) == 0)
        return -1;

    int offset = 0;
    if (format & 0x0001)
        offset += 2;
    if (format & 0x0002)
        offset += 2;
    return offset;
}
} // namespace

struct data
{
    float layout_units = 1000.f;
    bool valid = false;

    std::unordered_map<unsigned int, unsigned int> cmap;

    struct subtable
    {
        bool format2 = false;

        std::unordered_map<unsigned long long, float> pairs;

        pair_format2 classes;
    };

    std::vector<subtable> subtables;
};

namespace
{
void parse_cmap(const reader& r, size_t at, data& out)
{
    const unsigned int tables = r.u16(at + 2);

    size_t best = 0;
    int best_score = -1;
    for (unsigned int i = 0; i < tables; i++)
    {
        const size_t rec = at + 4 + i * 8;
        const unsigned int platform = r.u16(rec);
        const unsigned int encoding = r.u16(rec + 2);
        const size_t sub = at + r.u32(rec + 4);

        int score = -1;
        if (platform == 3 && encoding == 10)
            score = 3;
        else if (platform == 3 && encoding == 1)
            score = 2;
        else if (platform == 0)
            score = 1;

        if (score > best_score)
        {
            best_score = score;
            best = sub;
        }
    }

    if (best_score < 0)
        return;

    const unsigned int format = r.u16(best);
    if (format == 4)
    {
        const unsigned int seg_x2 = r.u16(best + 6);
        const unsigned int segs = seg_x2 / 2;
        const size_t end_at = best + 14;
        const size_t start_at = end_at + seg_x2 + 2;
        const size_t delta_at = start_at + seg_x2;
        const size_t range_at = delta_at + seg_x2;

        for (unsigned int s = 0; s < segs; s++)
        {
            const unsigned int end = r.u16(end_at + s * 2);
            const unsigned int start = r.u16(start_at + s * 2);
            const int delta = r.s16(delta_at + s * 2);
            const unsigned int range = r.u16(range_at + s * 2);

            if (start > end)
                continue;

            for (unsigned int c = start; c <= end && c != 0xFFFF; c++)
            {
                unsigned int glyph = 0;
                if (range == 0)
                {
                    glyph = (c + (unsigned int)delta) & 0xFFFF;
                }
                else
                {
                    const size_t at_glyph = range_at + s * 2 + range + (c - start) * 2;
                    glyph = r.u16(at_glyph);
                    if (glyph != 0)
                        glyph = (glyph + (unsigned int)delta) & 0xFFFF;
                }
                if (glyph != 0)
                    out.cmap[c] = glyph;
            }
        }
    }
    else if (format == 12)
    {
        const unsigned int groups = r.u32(best + 12);
        for (unsigned int g = 0; g < groups; g++)
        {
            const size_t rec = best + 16 + (size_t)g * 12;
            const unsigned int start = r.u32(rec);
            const unsigned int end = r.u32(rec + 4);
            const unsigned int glyph = r.u32(rec + 8);
            for (unsigned int c = start; c <= end && c - start < 0x10000; c++)
                out.cmap[c] = glyph + (c - start);
        }
    }
}

void parse_pair_pos(const reader& r, size_t at, data& out)
{
    const unsigned int format = r.u16(at);
    const unsigned int vf1 = r.u16(at + 4);
    const unsigned int vf2 = r.u16(at + 6);
    const int adv = x_advance_offset(vf1);
    const int rec1 = value_record_size(vf1);
    const int rec2 = value_record_size(vf2);

    if (adv < 0)
        return;

    if (format == 1)
    {
        coverage cov;
        cov.parse(r, at + r.u16(at + 2));

        const unsigned int sets = r.u16(at + 8);
        std::vector<unsigned int> first(cov.index.size(), 0);
        for (const auto& kv : cov.index)
            if (kv.second < first.size())
                first[kv.second] = kv.first;

        data::subtable sub;
        for (unsigned int s = 0; s < sets && s < first.size(); s++)
        {
            const size_t set_at = at + r.u16(at + 10 + s * 2);
            const unsigned int count = r.u16(set_at);
            for (unsigned int i = 0; i < count; i++)
            {
                const size_t pv = set_at + 2 + (size_t)i * (2 + rec1 + rec2);
                const unsigned int second = r.u16(pv);
                sub.pairs[((unsigned long long)first[s] << 20) | second] =
                    (float)r.s16(pv + 2 + adv);
            }
        }

        out.subtables.push_back(std::move(sub));
    }
    else if (format == 2)
    {
        pair_format2 classes;
        classes.cov.parse(r, at + r.u16(at + 2));
        classes.class1.parse(r, at + r.u16(at + 8));
        classes.class2.parse(r, at + r.u16(at + 10));
        classes.class1_count = r.u16(at + 12);
        classes.class2_count = r.u16(at + 14);

        if (classes.class1_count == 0 || classes.class2_count == 0)
            return;

        classes.values.assign((size_t)classes.class1_count * classes.class2_count, 0.f);
        const size_t stride = (size_t)(rec1 + rec2);
        for (unsigned int c1 = 0; c1 < classes.class1_count; c1++)
            for (unsigned int c2 = 0; c2 < classes.class2_count; c2++)
            {
                const size_t rec = at + 16 + ((size_t)c1 * classes.class2_count + c2) * stride;
                classes.values[(size_t)c1 * classes.class2_count + c2] = (float)r.s16(rec + adv);
            }

        data::subtable sub;
        sub.format2 = true;
        sub.classes = std::move(classes);
        out.subtables.push_back(std::move(sub));
    }
}

void parse_gpos(const reader& r, size_t at, data& out)
{
    const size_t feature_list = at + r.u16(at + 6);
    const size_t lookup_list = at + r.u16(at + 8);

    std::vector<unsigned int> wanted;
    const unsigned int features = r.u16(feature_list);
    for (unsigned int i = 0; i < features; i++)
    {
        const size_t rec = feature_list + 2 + (size_t)i * 6;
        if (!r.ok(rec, 6))
            break;
        if (memcmp(r.p + rec, "kern", 4) != 0)
            continue;

        const size_t feature = feature_list + r.u16(rec + 4);
        const unsigned int count = r.u16(feature + 2);
        for (unsigned int k = 0; k < count; k++)
            wanted.push_back(r.u16(feature + 4 + k * 2));
    }

    std::sort(wanted.begin(), wanted.end());
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());

    const unsigned int lookups = r.u16(lookup_list);
    for (unsigned int index : wanted)
    {
        if (index >= lookups)
            continue;

        const size_t lookup = lookup_list + r.u16(lookup_list + 2 + (size_t)index * 2);
        const unsigned int type = r.u16(lookup);
        const unsigned int subs = r.u16(lookup + 4);

        for (unsigned int s = 0; s < subs; s++)
        {
            const size_t sub = lookup + r.u16(lookup + 6 + (size_t)s * 2);
            if (type == 2)
                parse_pair_pos(r, sub, out);
            else if (type == 9)
            {
                const unsigned int real_type = r.u16(sub + 2);
                const size_t real = sub + r.u32(sub + 4);
                if (real_type == 2)
                    parse_pair_pos(r, real, out);
            }
        }
    }
}

data build(const std::vector<unsigned char>& blob)
{
    data out;
    reader r{blob.data(), blob.size()};
    if (blob.size() < 12)
        return out;

    const unsigned int tables = r.u16(4);
    size_t hhea = 0, cmap = 0, gpos = 0;
    for (unsigned int i = 0; i < tables; i++)
    {
        const size_t rec = 12 + (size_t)i * 16;
        if (!r.ok(rec, 16))
            break;

        if (memcmp(r.p + rec, "hhea", 4) == 0)
            hhea = r.u32(rec + 8);
        else if (memcmp(r.p + rec, "cmap", 4) == 0)
            cmap = r.u32(rec + 8);
        else if (memcmp(r.p + rec, "GPOS", 4) == 0)
            gpos = r.u32(rec + 8);
    }

    if (hhea)
        out.layout_units = (float)(r.s16(hhea + 4) - r.s16(hhea + 6));
    if (out.layout_units <= 0.f)
        out.layout_units = 1000.f;

    if (cmap)
        parse_cmap(r, cmap, out);
    if (gpos)
        parse_gpos(r, gpos, out);

    out.valid = true;
    return out;
}

std::map<const void*, data>& cache()
{
    static std::map<const void*, data> store;
    return store;
}
} // namespace

const data& get(const std::vector<unsigned char>& blob)
{
    auto& store = cache();
    const void* key = blob.data();

    auto it = store.find(key);
    if (it != store.end())
        return it->second;

    return store.emplace(key, build(blob)).first->second;
}

float layout_units(const data& d)
{
    return d.layout_units;
}

float pair(const data& d, unsigned int prev_codepoint, unsigned int codepoint)
{
    if (!d.valid || prev_codepoint == 0)
        return 0.f;

    auto g1 = d.cmap.find(prev_codepoint);
    auto g2 = d.cmap.find(codepoint);
    if (g1 == d.cmap.end() || g2 == d.cmap.end())
        return 0.f;

    const unsigned long long key = ((unsigned long long)g1->second << 20) | g2->second;

    for (size_t i = 0; i < d.subtables.size(); i++)
    {
        const data::subtable& sub = d.subtables[i];

        if (!sub.format2)
        {
            auto it = sub.pairs.find(key);
            if (it != sub.pairs.end())
                return it->second;
            continue;
        }

        const pair_format2& c = sub.classes;
        if (c.cov.find(g1->second) < 0)
            continue;

        const unsigned int c1 = c.class1.of(g1->second);
        const unsigned int c2 = c.class2.of(g2->second);
        if (c1 < c.class1_count && c2 < c.class2_count)
        {
            const float v = c.values[(size_t)c1 * c.class2_count + c2];
            if (v != 0.f)
                return v;
        }
    }

    return 0.f;
}
} // namespace solace::kerning
