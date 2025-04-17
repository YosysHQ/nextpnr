/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Peppercorn Authors.
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <boost/algorithm/string.hpp>

#include "pack.h"

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

namespace {
USING_NEXTPNR_NAMESPACE;

double calculate_delta_stepsize(double num)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(4) << num;
    std::string str = out.str();
    switch (4 - (str.size() - str.find_last_not_of('0') - 1)) {
    case 4:
        return 0.0001;
    case 3:
        return 0.001;
    case 2:
        return 0.01;
    case 1:
        return 0.1;
    default:
        return 1.0;
    }
}

int getDCO_optimized_value(int f_dco_min, int f_dco_max, double f_dco)
{
    /*
    optimization parameters
    DCO_OPT:
        1: Optimized to lower DCO frequency.
        2: Optimized to the middle of the DCO range (DEFAULT)
        3: Optimized to upper DCO frequency.
    */
    const int DCO_OPT = 2;              // 1: to lower DCO, 2: to DCO middle range, 3: to upper DCO, default = 2
    const int DCO_FREQ_OPT_FACTOR = 24; // proportional factor btw. frequency match and dco optimization

    switch (DCO_OPT) {
    case 1:
        return round(abs(f_dco - f_dco_min)) * DCO_FREQ_OPT_FACTOR;
    case 3:
        return round(abs(f_dco - f_dco_max)) * DCO_FREQ_OPT_FACTOR;
    default: {
        int f_dco_middle = (f_dco_max + f_dco_min) / 2; // Middle of DCO Range in MHz
        return round(abs(f_dco - f_dco_middle)) * DCO_FREQ_OPT_FACTOR;
    }
    }
}

void get_M1_M2(double f_core, double f_core_delta, PllCfgRecord &setting, double max_input_freq)
{
    // precise output frequency preference, highest priority
    // the larger value the higher priority
    const int OUT_FREQ_OPT_FACTOR = 4200000;

    std::vector<PllCfgRecord> res_arr;
    for (int M1 = 1; M1 <= 64; M1++) {
        for (int M2 = 1; M2 <= 1024; M2++) {
            if (((M1 * M2) % 2) != 0) {
                // M1*M2-->even number(except one);
                // this is important to ensure 90 degree phase shifting of clock output
                if (M1 * M2 != 1)
                    continue;
            }

            double f_core_local = setting.f_dco / (2 * setting.PDIV1 * M1 * M2);
            if (f_core_local > max_input_freq / 4)
                continue; // f_core max limit
            if ((f_core_local - f_core) < -f_core_delta)
                break; // lower limit jump to parent loop
            if (abs(f_core_local - f_core) > f_core_delta)
                continue; // reg limit continue
            if (setting.f_dco / setting.PDIV1 > max_input_freq)
                continue; // M1 input freq limit
            if ((setting.f_dco / setting.PDIV1) / M1 > max_input_freq / 2)
                continue; // M2 input freq limit

            PllCfgRecord tmp;
            tmp.f_core = f_core_local;
            tmp.M1 = M1;
            tmp.M2 = M2;
            tmp.weight = abs(f_core_local - f_core) * OUT_FREQ_OPT_FACTOR;
            res_arr.push_back(tmp);

            // frequency match
            if (abs(f_core_local - f_core) <= f_core_delta) {
                auto it = std::min_element(
                        res_arr.begin(), res_arr.end(),
                        [](const PllCfgRecord &a, const PllCfgRecord &b) { return a.weight < b.weight; });
                size_t index = std::distance(res_arr.begin(), it);
                setting.M1 = res_arr[index].M1;
                setting.M2 = res_arr[index].M2;
                setting.f_core = res_arr[index].f_core;
                setting.f_core_delta = abs(res_arr[index].f_core - f_core);
                setting.core_weight = res_arr[index].weight + setting.M1 + setting.M2 + setting.N1 + setting.N2 +
                                      setting.K + setting.PDIV1;
                return;
            }
        }
    }
}

void get_DCO_ext_feedback(double f_core, double f_ref, PllCfgRecord &setting, int f_dco_min, int f_dco_max,
                          double f_dco, double max_input_freq)
{
    std::vector<PllCfgRecord> res_arr;

    for (int M1 = 1; M1 <= 64; M1++) {
        for (int M2 = 1; M2 <= 1024; M2++) {
            if (((M1 * M2) % 2) != 0) {
                // M1*M2-->even number(except one);
                // this is important to ensure 90 degree phase shifting of clock output
                if (M1 * M2 != 1)
                    continue;
            }
            double f_dco_local = (f_ref / setting.K) * 2 * setting.PDIV1 * setting.N1 * setting.N2 * M1 * M2;
            if (f_dco_local > f_dco_max)
                break; // upper limit
            if ((f_dco_local < f_dco_min) || (f_dco_local > f_dco_max))
                continue; // dco out of range limit
            if (f_dco_local / setting.PDIV1 > max_input_freq)
                continue; // M1 input freq limit
            if ((f_dco_local / setting.PDIV1) / M1 > max_input_freq / 2)
                continue; // M2 input freq limit

            PllCfgRecord tmp;
            tmp.f_dco = f_dco_local;
            tmp.M1 = M1;
            tmp.M2 = M2;
            tmp.weight = f_dco_local + getDCO_optimized_value(f_dco_min, f_dco_max, f_dco_local);
            res_arr.push_back(tmp);
        }
    }

    auto it = std::min_element(res_arr.begin(), res_arr.end(),
                               [](const PllCfgRecord &a, const PllCfgRecord &b) { return a.weight < b.weight; });
    size_t index = std::distance(res_arr.begin(), it);
    setting.M1 = res_arr[index].M1;
    setting.M2 = res_arr[index].M2;
    setting.f_dco = res_arr[index].f_dco;
}
}; // namespace

NEXTPNR_NAMESPACE_BEGIN

PllCfgRecord GateMatePacker::get_pll_settings(double f_ref, double f_core, int mode, int low_jitter, bool pdiv0_mux,
                                              bool feedback)
{
    const int MATCH_LIMIT = 10; // only for low jitter = false
    // frequency tolerance for low jitter = false, the maximum frequency deviation in MHz from f_core
    const double DELTA_LIMIT = 0.1;

    double max_input_freq;
    int f_dco_min, f_dco_max, pdiv1_min;
    double f_dco;
    std::vector<PllCfgRecord> pll_cfg_arr;
    PllCfgRecord pll_cfg_rec;

    switch (mode) {
    case 1: {
        // low power
        f_dco_min = 500;
        f_dco_max = 1000;
        pdiv1_min = 1;
        max_input_freq = 1000;
        break;
    }
    case 2: {
        // economy
        f_dco_min = 1000;
        f_dco_max = 2000;
        pdiv1_min = 2;
        max_input_freq = 1250;
        break;
    }
    default: {
        // initialize as speed
        f_dco_min = 1250;
        f_dco_max = 2500;
        pdiv1_min = 2;
        max_input_freq = 1666.67;
        break;
    }
    }
    double f_core_par = f_core;

    if (f_ref > 50)
        log_warning("The PLL input frequency is outside the specified frequency (max 50 MHz ) range\n");

    if (pdiv0_mux && feedback) {
        double res;
        if (modf(f_core / f_ref, &res) != 0)
            log_warning("In this PLL mode f_core can only be greater and multiple of f_ref\n");
    }

    if (pdiv0_mux) {
        if (f_core > max_input_freq / 4) {
            f_core = max_input_freq / 4;
            log_warning("Frequency out of range; PLL max output frequency for mode: %d: %.5f MHz\n", mode,
                        max_input_freq / 4);
        }
    }

    pll_cfg_rec.K = 1;
    pll_cfg_rec.N1 = 1;
    pll_cfg_rec.N2 = 1;
    pll_cfg_rec.M1 = 1;
    pll_cfg_rec.M2 = 1;
    pll_cfg_rec.PDIV1 = 2;
    pll_cfg_rec.f_dco = 0;
    pll_cfg_rec.f_core_delta = std::numeric_limits<double>::max();
    pll_cfg_rec.f_core = pll_cfg_rec.f_dco / (2 * pll_cfg_rec.PDIV1);
    pll_cfg_rec.core_weight = std::numeric_limits<double>::max();
    pll_cfg_arr.push_back(pll_cfg_rec);

    double f_core_delta_stepsize = calculate_delta_stepsize(f_core);

    int K = 1;
    int K_max = low_jitter ? 1 : 1024;
    int match_cnt = 0;
    int match_delta_cnt = 0;
    while (K <= K_max) {
        for (int N1 = 1; N1 <= 64; N1++) {
            for (int N2 = 1; N2 <= 1024; N2++) {
                for (int PDIV1 = pdiv1_min; PDIV1 <= 2; PDIV1++) {
                    if (feedback) {
                        // extern feedback
                        int f_core_local = (f_ref / K) * N1 * N2;
                        if (f_core_local > max_input_freq / 4)
                            continue;
                        if (abs(f_core - f_core_local) < pll_cfg_arr[0].f_core_delta) {
                            // search for best frequency match
                            pll_cfg_arr[0].f_core = f_core_local;
                            pll_cfg_arr[0].f_core_delta = abs(f_core - f_core_local);
                            pll_cfg_arr[0].K = K;
                            pll_cfg_arr[0].N1 = N1;
                            pll_cfg_arr[0].N2 = N2;
                            pll_cfg_arr[0].PDIV1 = PDIV1;
                        }
                        if (pll_cfg_arr[0].f_core_delta == 0) {
                            get_DCO_ext_feedback(f_core, f_ref, pll_cfg_arr[0], f_dco_min, f_dco_max, f_dco,
                                                 max_input_freq);
                            log_info("PLL fout= %.4f MHz (fout error %.5f%% of requested %.4f MHz)\n",
                                     pll_cfg_arr[0].f_core,
                                     100 - (100 * std::min(pll_cfg_arr[0].f_core, f_core_par) /
                                            std::max(pll_cfg_arr[0].f_core, f_core_par)),
                                     f_core);
                            return pll_cfg_arr[0];
                        }
                    } else {
                        f_dco = (f_ref / K) * PDIV1 * N1 * N2;

                        if ((f_dco <= f_dco_min) || (f_dco > f_dco_max))
                            continue; // DCO out of range
                        if (f_dco == 0)
                            continue; // DCO = 0
                        if (f_dco / PDIV1 > max_input_freq)
                            continue; // N1 input freq limit
                        if ((f_dco / PDIV1) / N1 > max_input_freq / 2)
                            continue; // N2 input freq limit
                        if (int(f_core) > int(f_dco / (2 * PDIV1)))
                            continue; // > f_core max

                        pll_cfg_rec.f_core = 0;
                        pll_cfg_rec.f_dco = f_dco;
                        pll_cfg_rec.f_core_delta = std::numeric_limits<double>::max();
                        pll_cfg_rec.K = K;
                        pll_cfg_rec.N1 = N1;
                        pll_cfg_rec.N2 = N2;
                        pll_cfg_rec.PDIV1 = PDIV1;
                        pll_cfg_rec.M1 = 0;
                        pll_cfg_rec.M2 = 0;
                        pll_cfg_rec.core_weight = std::numeric_limits<double>::max();

                        if (!pdiv0_mux) {
                            // f_core = f_dco/2
                            pll_cfg_rec.M1 = 1;
                            pll_cfg_rec.M2 = 1;
                            pll_cfg_rec.f_core = pll_cfg_rec.f_dco / 2;
                            pll_cfg_rec.core_weight = abs(pll_cfg_rec.f_core - f_core);
                        } else {
                            // default clock path
                            // calculate M1, M2 then calculate weight for intern loop feedback
                            bool found = false;
                            double f_core_delta = 0;
                            while (f_core_delta < (round((max_input_freq / 4) - f_ref / K))) {
                                get_M1_M2(f_core, f_core_delta, pll_cfg_rec, max_input_freq);
                                if (pll_cfg_rec.f_core != 0 && pll_cfg_rec.f_core_delta <= f_core_delta) {
                                    // best result
                                    pll_cfg_rec.core_weight +=
                                            pll_cfg_rec.f_dco + getDCO_optimized_value(f_dco_min, f_dco_max, f_dco);
                                    found = true;
                                }
                                if (found)
                                    break; // best result found
                                f_core_delta += f_core_delta_stepsize;
                            }
                        }
                        pll_cfg_arr.push_back(pll_cfg_rec);
                        if (pll_cfg_rec.f_core_delta == 0)
                            match_cnt++;
                        if (pll_cfg_rec.f_core_delta < DELTA_LIMIT)
                            match_delta_cnt++;
                    }
                }
            }
        }
        K++;
        // only with low_jitter false
        if (match_cnt > MATCH_LIMIT)
            break;
        if ((match_cnt == 0) && (match_delta_cnt > MATCH_LIMIT))
            break;
    }
    if (feedback) {
        // extern feedback if not exact match pick the best here
        get_DCO_ext_feedback(f_core, f_ref, pll_cfg_arr[0], f_dco_min, f_dco_max, f_dco, max_input_freq);
    }
    auto it =
            std::min_element(pll_cfg_arr.begin(), pll_cfg_arr.end(), [](const PllCfgRecord &a, const PllCfgRecord &b) {
                return a.core_weight < b.core_weight;
            });
    PllCfgRecord val = pll_cfg_arr.at(std::distance(pll_cfg_arr.begin(), it));
    log_info("PLL fout= %.4f MHz (fout error %.5f%% of requested %.4f MHz)\n", val.f_core,
             100 - (100 * std::min(val.f_core, f_core_par) / std::max(val.f_core, f_core_par)), f_core);
    return val;
}

NEXTPNR_NAMESPACE_END
