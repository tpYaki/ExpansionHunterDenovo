//
// ExpansionHunter Denovo
// Copyright 2016-2019 Illumina, Inc.
// All rights reserved.
//
// Author: Egor Dolzhenko <edolzhenko@illumina.com>,
//         Michael Eberle <meberle@illumina.com>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "reads/IrrFinder.hh"

#include <algorithm>
#include <boost/bind.hpp>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/SequenceUtils.hh"
#include "reads/Purity.hh"

using std::logic_error;
using std::map;
using std::string;
using std::to_string;
using std::unordered_map;
using std::vector;

int32_t MaxMatchesAtOffset(int32_t offset, const std::string& bases)
{
    if (bases.length() < offset)
        return 0;
    return bases.length() - offset;
}

double MatchFrequencyAtOffset(int32_t offset, const string& bases)
{
    if (offset <= 0 || bases.length() / 2 + 1 <= offset)
        throw logic_error(to_string(offset) + " is not a valid offset for " + bases);

    int32_t num_matches = 0;
    for (size_t position = 0; position != bases.length() - offset; ++position)
        if (bases[position] == bases[position + offset])
            ++num_matches;

    const int32_t max_matches = MaxMatchesAtOffset(offset, bases);
    const double match_frequency = (double)num_matches / max_matches;
    return match_frequency;
}

int32_t SmallestFrequentPeriod(double min_frequency, const string& bases)
{
    for (int32_t offset = 1; offset != bases.length() / 2 + 1; ++offset)
    {
        const double match_frequency = MatchFrequencyAtOffset(offset, bases);
        if (match_frequency >= min_frequency)
            return offset;
    }

    return -1;
}

char ExtractConsensusBase(int32_t offset, int32_t period, const std::string& bases)
{
    unordered_map<char, int32_t> char_frequency;
    for (int32_t index = offset; index < bases.length(); index += period)
    {
        ++char_frequency[bases[index]];
    }

    char consensus_char = '?';
    int32_t max_frequency = 0;
    for (const auto& kv : char_frequency)
    {
        char current_char = kv.first;
        int32_t current_frequency = kv.second;
        if (current_frequency > max_frequency)
        {
            max_frequency = current_frequency;
            consensus_char = current_char;
        }
    }

    return consensus_char;
}

string ExtractConsensusRepeatUnit(double period, const std::string& bases)
{
    string repeat_unit;
    for (int32_t offset = 0; offset != period; ++offset)
        repeat_unit += ExtractConsensusBase(offset, period, bases);

    return repeat_unit;
}

string MinimialUnitUnderShift(const string& unit)
{
    string minimal_unit = unit;
    const string double_unit = unit + unit;
    for (int32_t index = 0; index != unit.length(); ++index)
    {
        string current_unit = double_unit.substr(index, unit.length());
        if (current_unit < minimal_unit)
            minimal_unit = current_unit;
    }
    return minimal_unit;
}

string ComputeCanonicalRepeatUnit(const string& unit)
{
    const string minimal_unit = MinimialUnitUnderShift(unit);

    const string unit_rc = reverseComplement(unit);
    const string minimal_unit_rc = MinimialUnitUnderShift(unit_rc);

    if (minimal_unit_rc < minimal_unit)
        return minimal_unit_rc;
    return minimal_unit;
}

string ComputeCanonicalRepeatUnit(double min_frequency, const string& bases)
{
    const int32_t period = SmallestFrequentPeriod(min_frequency, bases);
    if (period == -1)
        return "";
    string repeat_unit = ExtractConsensusRepeatUnit(period, bases);
    const double kPerfectMatchFrequency = 1.0;
    const int32_t reduced_period = SmallestFrequentPeriod(kPerfectMatchFrequency, repeat_unit);
    if (reduced_period != -1 && reduced_period != period)
        repeat_unit = ExtractConsensusRepeatUnit(reduced_period, repeat_unit);
    repeat_unit = ComputeCanonicalRepeatUnit(repeat_unit);
    return repeat_unit;
}

bool IsInrepeatRead(const string& bases, const string& quals, string& unit)
{
    const double min_frequency = 0.8;
    unit = ComputeCanonicalRepeatUnit(min_frequency, bases);
    if (unit.empty() || unit == "N")
        return false;

    const vector<string> units = { unit };
    const vector<vector<string>> units_shifts = ShiftUnits(units);

    double score = MatchRepeatRc(units_shifts, bases, quals);
    score /= bases.length();

    const double min_score = 0.90;
    if (score < min_score)
    {
        return false;
    }

    return true;
}