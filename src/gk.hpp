#pragma once

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

// The Greenwald-Khanna algorithm as defined in the paper
// Space-Efficient Online Computation of Quantile Summaries

namespace stmpct {

// TODO: Rewrite this class from precomputing bands to compugit ting them
// on the fly
class gk_bands {
  private:
    int _two_epsilon_n;
  public:
    gk_bands(int two_epsilon_n) : _two_epsilon_n(two_epsilon_n) {}

    int operator[](int delta) const {
        if (delta == _two_epsilon_n)
            return 0; // delta = two_epsilon_n is band 0 by definition
        else if (delta == 0)
            return std::numeric_limits<int>::max(); // delta = 0 is its own band

        int alpha = 1;
        for (;;) {
            int two_alpha = (1 << alpha);
            int lower = _two_epsilon_n - two_alpha - (_two_epsilon_n % two_alpha);
            if (lower < 0)
                lower = 0;
            int two_alpha_minus_1 = (1 << (alpha - 1));
            int upper = _two_epsilon_n - two_alpha_minus_1 - (_two_epsilon_n % two_alpha_minus_1);
            if (delta >= lower + 1 && delta <= upper)
                return alpha;
            ++alpha;
        }
    }
};

template <typename T> class gk {
  public:
    explicit gk(double epsilon)
        : m_epsilon(epsilon), m_one_over_2e((int)(1 / (2 * epsilon))),
          m_n(0), m_S{} {}

    void insert(T v) {
        if (m_n % m_one_over_2e == 0)
            compress();
        do_insert(v);
        ++m_n;
    }

    T quantile(double phi) const {
        double en = m_epsilon * m_n;
        int r = (int)ceil(phi * m_n);
        double rmin = 0;
        for (auto it = m_S.begin(); it != m_S.end(); ++it) {
            rmin += it->g;
            double rmax = rmin + it->delta;
            if (r - rmin <= en && rmax - r <= en)
                return it->v;
        }

        // Should never reach the below
        assert(false);
        return T{};
    }

  private:
    struct tuple {
        T v;
        int g;
        int delta;

        tuple(T v, int g, int delta) : v(v), g(g), delta(delta) {}
    };
    typedef std::vector<tuple> tuples_t;
    typedef typename tuples_t::iterator tuples_iterator;

    int determine_delta(tuples_iterator it) {
        if (m_n <= m_one_over_2e) {
            return 0;
        } else if (it == m_S.begin() || it == m_S.end()) {
            return 0;
        } else {
            int delta = (int)floor(2 * m_epsilon * m_n) - 1;
            assert(delta >= 0 && delta <= 2 * m_epsilon * m_n - 1);
            return delta;
        }
    }

    tuples_iterator find_insertion_index(T v) {
        tuples_iterator it = m_S.begin();
        // Use it->v < v rather than v >= it->v to minimize the requirements on
        // T
        while (it != m_S.end() && it->v < v)
            ++it;
        return it;
    }

    void do_insert(T v) {
        tuples_iterator it = find_insertion_index(v);
        tuple t(v, 1, determine_delta(it));
        m_S.insert(it, t);
    }

    void compress() {
        if (m_S.empty())
            return;

        int two_epsilon_n = (int)(2 * m_epsilon * m_n);
        gk_bands bands(two_epsilon_n);
        tuples_iterator it = prev(prev(m_S.end()));
        while (it != m_S.begin()) {
            tuples_iterator it2 = next(it);
            if (bands[it->delta] <= bands[it2->delta]) {
                int g_i_star = it->g;
                tuples_iterator start_it = it;
                while (start_it != next(m_S.begin()) &&
                       bands[prev(start_it)->delta] < bands[it->delta]) {
                    --start_it;
                    g_i_star += start_it->g;
                }
                if ((g_i_star + it2->g + it2->delta) < two_epsilon_n) {
                    tuple merged(it2->v, g_i_star + it2->g, it2->delta);
                    *start_it = merged;
                    m_S.erase(next(start_it), next(it2));
                    it = start_it;
                }
            }
            --it;
        }
    }

    double m_epsilon;
    int m_one_over_2e;
    int m_n;
    tuples_t m_S;
};
} // namespace stmpct

