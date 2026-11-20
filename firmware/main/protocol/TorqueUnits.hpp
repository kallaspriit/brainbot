#pragma once

#include <cstdint>

/**
 * Converts between the drive's torque per mille (6071, 6078: 0.1 % of the rated current) and 0.1 mN·m with the rated
 * torque 6076 (Torque mode plan). Rounded both ways, so a value set in 0.1 mN·m comes back as set: 0.1 % of an 80 mN·m
 * motor is 0.08 mN·m, finer than the 0.1 mN·m shown. Nominal figures, not a measurement.
 */
namespace TorqueUnits {
/**
 * Converts per mille of the rated current to 0.1 mN·m.
 *
 * @param perMille Torque or current, 0.1 % of rated
 * @param ratedMilliNm Rated torque 6076 in mN·m; 0 if unknown
 * @return 0.1 mN·m, 0 if the rated torque is unknown
 */
constexpr int32_t toDeciMilliNm(int32_t perMille, int32_t ratedMilliNm) {
    int64_t scaled = static_cast<int64_t>(perMille) * ratedMilliNm;

    return static_cast<int32_t>(scaled >= 0 ? (scaled + 50) / 100 : (scaled - 50) / 100);
}

/**
 * Converts 0.1 mN·m to per mille of the rated current.
 *
 * @param deciMilliNm Torque in 0.1 mN·m
 * @param ratedMilliNm Rated torque 6076 in mN·m; 0 if unknown
 * @return 0.1 % of rated, 0 if the rated torque is unknown
 */
constexpr int32_t toPerMille(int32_t deciMilliNm, int32_t ratedMilliNm) {
    if (ratedMilliNm <= 0) {
        return 0;
    }

    int64_t scaled = static_cast<int64_t>(deciMilliNm) * 100;
    int64_t half = ratedMilliNm / 2;

    return static_cast<int32_t>(scaled >= 0 ? (scaled + half) / ratedMilliNm : (scaled - half) / ratedMilliNm);
}
} // namespace TorqueUnits
