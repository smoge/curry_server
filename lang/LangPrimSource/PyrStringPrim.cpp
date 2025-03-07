/*
    SuperCollider real time audio synthesis system
    Copyright (c) 2002 James McCartney. All rights reserved.
    http://www.audiosynth.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

/*

Primitives for String.

*/

#include "PyrPrimitive.h"
#include "PyrKernel.h"
#include "GC.h"
#include "Hash.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <vector>
#include "PyrLexer.h"
#include "SC_Filesystem.hpp"
#include "SC_Codecvt.hpp" // path_to_utf8_str
#ifdef _WIN32
#    include <direct.h>
#    include "SC_Win32Utils.h"
#else
#    include <sys/param.h>
#endif

#include <boost/regex.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/unordered_set.hpp>

#include <fstream>
#include <filesystem>

#include <yaml-cpp/yaml.h>

using namespace std;
namespace fs = std::filesystem;

int prStringAsSymbol(struct VMGlobals* g, int numArgsPushed) {
    PyrSlot* a;
    char str[1024], *strp = nullptr;
    int len;

    a = g->sp;
    len = slotRawObject(a)->size;
    strp = len > 1023 ? (char*)malloc(len + 1) : str;

    memcpy(strp, slotRawString(a)->s, len);
    strp[len] = 0;

    SetSymbol(a, getsym(strp));

    if (len > 1023)
        free(strp);

    return errNone;
}

int prString_AsInteger(struct VMGlobals* g, int numArgsPushed) {
    PyrSlot* a = g->sp;

    char str[256];
    int err = slotStrVal(a, str, 255);
    if (err)
        return err;

    SetInt(a, atoi(str));

    return errNone;
}

int prString_AsFloat(struct VMGlobals* g, int numArgsPushed) {
    PyrSlot* a = g->sp;

    char str[256];
    int err = slotStrVal(a, str, 255);
    if (err)
        return err;

    SetFloat(a, atof(str));

    return errNone;
}

int prString_AsCompileString(struct VMGlobals* g, int numArgsPushed) {
    PyrSlot* a = g->sp;
    PyrString* scstr = slotRawString(a);
    char* chars1 = scstr->s;
    int newSize = scstr->size + 2;
    for (int i = 0; i < scstr->size; ++i) {
        if (chars1[i] == '"' || chars1[i] == '\\')
            newSize++;
    }
    PyrString* newString = newPyrStringN(g->gc, newSize, 0, true);
    char* chars2 = newString->s;
    chars2[0] = '"';
    chars2[newSize - 1] = '"';
    int k = 1;
    for (int i = 0; i < scstr->size; ++i) {
        int c = chars1[i];
        if (c == '"' || c == '\\')
            chars2[k++] = '\\';
        chars2[k++] = c;
    }
    SetObject(a, newString);
    return errNone;
}

int prString_Format(struct VMGlobals* g, int numArgsPushed) {
    PyrSlot* a = g->sp - 1;
    PyrSlot* b = g->sp;

    if (!isKindOfSlot(b, class_array))
        return errWrongType;

    char* fmt = slotRawString(a)->s;

    int asize = slotRawObject(a)->size;
    int bsize = slotRawObject(b)->size;
    int csize = asize;

    PyrSlot* slots = slotRawObject(b)->slots;
    for (int i = 0; i < bsize; ++i) {
        PyrSlot* slot = slots + i;
        if (!isKindOfSlot(slot, class_string))
            return errWrongType;
        csize += slotRawString(slot)->size;
    }
    PyrString* newString = newPyrStringN(g->gc, csize, 0, true);
    char* buf = newString->s;

    int k = 0;
    int index = 0;
    for (int i = 0; i < asize;) {
        char ch = fmt[i++];
        if (ch == '%') {
            if (index < bsize) {
                PyrString* bstring = slotRawString(&slots[index]);
                memcpy(buf + k, bstring->s, bstring->size);
                k += bstring->size;
                index++;
            }
        } else if (ch == '\\') {
            if (i >= asize)
                break;
            ch = fmt[i++];
            if (ch == '%') {
                buf[k++] = '%';
            } else {
                i--;
                buf[k++] = '\\';
            }
        } else {
            buf[k++] = ch;
        }
    }
    newString->size = k;
    SetObject(a, newString);
    return errNone;
};

namespace detail {

namespace bin = boost::intrusive;

class regex_lru_cache {
    int regex_flags;

    struct regex_node : bin::list_base_hook<>, bin::unordered_set_base_hook<> {
    public:
        regex_node(const char* str, size_t size, int regex_flags): pattern(str, size, regex_flags) {}

        boost::regex const& get(void) const { return pattern; }

    private:
        boost::regex pattern;
    };

    struct regex_equal {
        bool operator()(regex_node const& lhs, regex_node const& rhs) const { return lhs.get() == rhs.get(); }

        bool operator()(const char* lhs, regex_node const& rhs) const {
            return strcmp(lhs, rhs.get().str().c_str()) == 0;
        }
    };

    static inline std::size_t string_hash(const char* str) {
        std::size_t ret = 0;

        // sdbm hash
        int c;
        while ((c = *str++))
            ret = c + (ret << 6) + (ret << 16) - ret;

        return ret;
    }

    struct regex_hash {
        size_t operator()(regex_node const& arg) const { return string_hash(arg.get().str().c_str()); }

        size_t operator()(const char* arg) const { return string_hash(arg); }
    };

    typedef bin::unordered_set<regex_node, bin::equal<regex_equal>, bin::hash<regex_hash>, bin::power_2_buckets<true>,
                               bin::constant_time_size<false>>
        re_set_t;
    typedef re_set_t::bucket_type bucket_type;
    typedef re_set_t::bucket_traits bucket_traits;
    bucket_type buckets[128];
    re_set_t re_set;

    bin::list<regex_node> re_list;

    void pop_lru() {
        regex_node& rlu = re_list.back();
        re_list.pop_back();
        re_set.erase(rlu);
        delete &rlu;
    }

public:
    regex_lru_cache(int regex_flags = boost::regex_constants::ECMAScript):
        re_set(bucket_traits(buckets, 128)),
        re_list() {}

    ~regex_lru_cache() {
        while (!re_list.empty()) {
            pop_lru();
        }
    }

    boost::regex const& get_regex(const char* str, size_t size) {
        re_set_t::iterator re_in_cache = re_set.find(str, regex_hash(), regex_equal());
        if (re_in_cache != re_set.end()) {
            regex_node& node = *re_in_cache;
            bin::list<regex_node>::iterator re_in_list = bin::list<regex_node>::s_iterator_to(node);

            re_list.splice(re_list.begin(), re_list, re_in_list); // move to the begin of the list
            assert(&re_list.front() == &node);
            return node.get();
        }

        if (re_list.size() >= 64)
            pop_lru();

        regex_node* new_node = new regex_node(str, size, regex_flags);
        re_set.insert(*new_node);
        re_list.push_front(*new_node);
        return new_node->get();
    }
};

}


int prString_ReplaceRegex(struct VMGlobals* g, int numArgsPushed) {
    // caches the last 64 boost:regex instances.
    static detail::regex_lru_cache regex_lru_cache(boost::regex_constants::ECMAScript | boost::regex_constants::nosubs);


    PyrSlot* slot_this = g->sp - 2; // source string
    PyrSlot* slot_regex = g->sp - 1; // find
    PyrSlot* slot_replace = g->sp; // replace with

    // slot one does not need to be checked as this method should only be called from methods in String,
    //    or children thereof.
    if (!isKindOfSlot(slot_regex, class_string)) {
        SetNil(slot_this);
        return errWrongType;
    }
    if (!isKindOfSlot(slot_replace, class_string)) {
        SetNil(slot_this);
        return errWrongType;
    }

    try {
        const auto& pattern = regex_lru_cache.get_regex(slotRawString(slot_regex)->s, slotRawString(slot_regex)->size);

        const char* source_start = slotRawString(slot_this)->s;
        const int source_size = slotRawString(slot_this)->size;

        if (source_size < 0) { // size is signed
            SetNil(slot_this);
            error("String has negative size\n");
            return errFailed;
        }

        std::string out {};
        // PyrStrings are not null terminated so a copy is needed.
        const auto [replaceError, replace] = slotStrStdStrVal(slot_replace);
        if (replaceError != errNone) {
            SetNil(slot_this);
            return replaceError;
        }

        boost::regex_replace(std::back_inserter(out), source_start, source_start + source_size, pattern, replace);

        if (out.size() > std::numeric_limits<decltype(PyrObjectHdr {}.size)>::max()) {
            SetNil(slot_this);
            error("String too big\n");
            return errNone;
        }
        SetObject(slot_this, newPyrStringN(g->gc, static_cast<int>(out.size()), 0, true));
        std::copy(out.begin(), out.end(), slotRawString(slot_this)->s);
        return errNone;
    } catch (const std::exception& e) {
        postfl("Warning: Exception in _String_ReplaceRegex -%s\n", e.what());
        SetNil(slot_this);
        return errFailed;
    };
}

int prString_Regexp(struct VMGlobals* g, int numArgsPushed) {
    /* not reentrant */
    static detail::regex_lru_cache regex_lru_cache(boost::regex_constants::ECMAScript | boost::regex_constants::nosubs);

    using namespace boost;

    int start, end, len;

    PyrSlot* a = g->sp - 3;
    PyrSlot* b = g->sp - 2;
    PyrSlot* c = g->sp - 1;
    PyrSlot* d = g->sp;

    if (!isKindOfSlot(b, class_string))
        return errWrongType;
    if (NotInt(c) || (NotInt(d) && NotNil(d)))
        return errWrongType;
    start = slotRawInt(c);

    len = slotRawObject(b)->size; // last char index instead of size

    if (IsNil(d)) {
        end = len;
    } else {
        end = slotRawInt(d);
    }

    if (end > len)
        end = len;

    if (end - start <= 0) {
        SetFalse(a);
        return errNone;
    }

    int stringlen = end - start;

    try {
        regex const& pattern = regex_lru_cache.get_regex(slotRawString(a)->s, slotRawObject(a)->size);
        match_flag_type flags = match_nosubs | match_any;

        const char* stringStart = slotRawString(b)->s + start;
        const char* stringEnd = stringStart + stringlen;
        bool res = regex_search(stringStart, stringEnd, pattern, flags);

        if (res)
            SetTrue(a);
        else
            SetFalse(a);

        return errNone;
    } catch (std::exception const& e) {
        postfl("Warning: Exception in _String_Regexp - %s\n", e.what());
        return errFailed;
    }
}

struct sc_regexp_match {
    int pos;
    int len;
};


static int prString_FindRegexp(struct VMGlobals* g, int numArgsPushed) {
    /* not reentrant */
    static detail::regex_lru_cache regex_lru_cache(boost::regex_constants::ECMAScript);

    using namespace boost;

    PyrSlot* a = g->sp - 2; // source string
    PyrSlot* b = g->sp - 1; // pattern
    PyrSlot* c = g->sp; // offset

    if (!isKindOfSlot(b, class_string) || (NotInt(c))) {
        SetNil(a);
        return errWrongType;
    }

    int offset = slotRawInt(c);
    int stringlen = std::max(slotRawObject(a)->size - offset, 0);

    std::vector<sc_regexp_match> matches;
    const char* const stringBegin = slotRawString(a)->s + offset;
    try {
        regex const& pattern = regex_lru_cache.get_regex(slotRawString(b)->s, slotRawObject(b)->size);
        match_flag_type flags = match_default;

        match_results<const char*> what;
        const char* start = stringBegin;
        const char* end = start + stringlen;
        while (start <= end && regex_search(start, end, what, pattern, flags)) {
            for (int i = 0; i < what.size(); ++i) {
                sc_regexp_match match;
                if (what[i].matched) {
                    match.pos = what[i].first - stringBegin;
                    match.len = what[i].second - what[i].first;
                } else {
                    match.pos = 0;
                    match.len = 0;
                }
                matches.push_back(match);
            }
            start = what[0].second;
            if (what[0].first == what[0].second)
                ++start;
        }
    } catch (std::exception const& e) {
        postfl("Warning: Exception in _String_FindRegexp - %s\n", e.what());
        SetNil(a);
        return errFailed;
    }

    int match_count = matches.size();

    PyrObject* result_array = newPyrArray(g->gc, match_count, 0, true);
    ++g->sp; // advance the stack to avoid overwriting receiver
    SetObject(g->sp, result_array); // push result to make reachable

    for (int i = 0; i < match_count; ++i) {
        int pos = matches[i].pos;
        int len = matches[i].len;

        PyrObject* array = newPyrArray(g->gc, 2, 0, true);
        SetObject(result_array->slots + i, array);
        result_array->size++;
        g->gc->GCWriteNew(result_array, array); // we know array is white so we can use GCWriteNew

        PyrString* matched_string = newPyrStringN(g->gc, len, 0, true);
        memcpy(matched_string->s, stringBegin + pos, len);

        array->size = 2;
        SetInt(array->slots, pos + offset);
        SetObject(array->slots + 1, matched_string);
        g->gc->GCWriteNew(array, matched_string); // we know matched_string is white so we can use GCWriteNew
    };

    --g->sp; // pop the stack back to the receiver slot since we stored result_array there above
    SetObject(a, result_array); // now we can set the result in a

    return errNone;
}

static int prString_FindRegexpAt(struct VMGlobals* g, int numArgsPushed) {
    /* not reentrant */
    static detail::regex_lru_cache regex_lru_cache(boost::regex_constants::ECMAScript);

    using namespace boost;

    PyrSlot* a = g->sp - 2; // source string
    PyrSlot* b = g->sp - 1; // pattern
    PyrSlot* c = g->sp; // offset

    if (!isKindOfSlot(b, class_string) || (NotInt(c))) {
        SetNil(a);
        return errWrongType;
    }

    int offset = slotRawInt(c);
    int stringlen = std::max(slotRawObject(a)->size - offset, 0);

    int matched_len = 0;

    const char* const stringBegin = slotRawString(a)->s + offset;
    try {
        regex const& pattern = regex_lru_cache.get_regex(slotRawString(b)->s, slotRawObject(b)->size);

        // match_continuous: the match must begin at the offset start
        match_flag_type flags = match_continuous;

        match_results<const char*> what;
        const char* start = stringBegin;
        const char* end = start + stringlen;
        if (regex_search(start, end, what, pattern, flags)) {
            assert(what[0].first == stringBegin);
            matched_len = what[0].second - what[0].first;
        } else {
            SetNil(a);
            return errNone;
        }
    } catch (std::exception const& e) {
        postfl("Warning: Exception in _String_FindRegexpAt - %s\n", e.what());
        return errFailed;
    }

    PyrObject* array = newPyrArray(g->gc, 2, 0, true);
    ++g->sp;
    SetObject(g->sp, array); // push on stack to make reachable

    PyrString* matched_string = newPyrStringN(g->gc, matched_len, 0, true);
    memcpy(matched_string->s, stringBegin, (size_t)matched_len);

    array->size = 2;
    SetInt(array->slots + 1, matched_len);
    SetObject(array->slots, matched_string);
    g->gc->GCWriteNew(array, matched_string); // we know matched_string is white so we can use GCWriteNew
    --g->sp; // pop the stack back to the receiver slot since we stored array there above
    SetObject(a, array); // now we can set the result in a

    return errNone;
}

int memcmpi(char* a, char* b, int len) {
    for (int i = 0; i < len; ++i) {
        char aa = toupper(a[i]);
        char bb = toupper(b[i]);
        if (aa < bb)
            return -1;
        if (aa > bb)
            return 1;
    }
    return 0;
}

int prStringCompare(struct VMGlobals* g, int numArgsPushed) {
    PyrSlot *a, *b, *c;
    int cmp, length;

    a = g->sp - 2;
    b = g->sp - 1;
    c = g->sp;

    if (NotObj(b) || !isKindOf(slotRawObject(b), class_string))
        return errWrongType;

    length = sc_min(slotRawObject(a)->size, slotRawObject(b)->size);
    if (IsTrue(c))
        cmp = memcmpi(slotRawString(a)->s, slotRawString(b)->s, length);
    else
        cmp = memcmp(slotRawString(a)->s, slotRawString(b)->s, length);
    if (cmp == 0) {
        if (slotRawObject(a)->size < slotRawObject(b)->size)
            cmp = -1;
        else if (slotRawObject(a)->size > slotRawObject(b)->size)
            cmp = 1;
    }
    SetInt(a, cmp);
    return errNone;
}

int prStringHash(struct VMGlobals* g, int numArgsPushed) {
    PyrSlot* a = g->sp;
    int hash = Hash(slotRawString(a)->s, slotRawString(a)->size);
    SetInt(a, hash);
    return errNone;
}

int prString_PathMatch(struct VMGlobals* g, int numArgsPushed) {
    PyrSlot* a = g->sp;
    char pattern[PATH_MAX];
    int err = slotStrVal(a, pattern, PATH_MAX - 1);
    if (err)
        return err;

    SC_Filesystem::Glob* glob = SC_Filesystem::makeGlob(pattern);

    // exit early with empty array if no matches found
    if (!glob) {
        SetObject(a, newPyrArray(g->gc, 0, 0, true));
        return errNone;
    }

    // read all paths into a vector
    std::vector<fs::path> paths;
    while (true) {
        const fs::path& matched_path = SC_Filesystem::globNext(glob);
        if (matched_path.empty())
            break;
        else
            paths.push_back(matched_path);
    };

    // create array with appropriate reserved size
    PyrObject* array = newPyrArray(g->gc, paths.size(), 0, true);
    SetObject(a, array); // this is okay here as we don't use the receiver below

    // convert paths and copy into sclang array.
    for (int i = 0; i < paths.size(); ++i) {
        const std::string& matched_path_utf8 = SC_Codecvt::path_to_utf8_str(paths[i]);
        PyrObject* string = (PyrObject*)newPyrString(g->gc, matched_path_utf8.c_str(), 0, true);
        SetObject(array->slots + i, string);
        g->gc->GCWriteNew(array, string); // we know string is white so we can use GCWriteNew
        array->size++;
    }

    SC_Filesystem::freeGlob(glob);
    return errNone;
}

int prString_Getenv(struct VMGlobals* g, int /* numArgsPushed */) {
    PyrSlot* slot_this = g->sp;
    const auto [this_err, this_str] = slotStdStrVal(slot_this);
    if (this_err != errNone)
        return this_err;

#ifdef _WIN32
    const auto this_str_w = SC_Codecvt::utf8_cstr_to_utf16_wstring(this_str.c_str());
    const auto count = GetEnvironmentVariableW(this_str_w.c_str(), nullptr, 0);
    std::string buf;
    if (count != 0) {
        std::wstring wbuf(count, 0);
        GetEnvironmentVariableW(this_str_w.c_str(), wbuf.data(), count);
        buf = SC_Codecvt::utf16_wcstr_to_utf8_string(wbuf.c_str());
    }
    char* value = count != 0 ? buf.data() : nullptr;
#else
    char* value = getenv(this_str.c_str());
#endif

    if (value == nullptr) {
        SetNil(slot_this);
        return errNone; // returns nil if not present
    }

    PyrString* pyrString = newPyrString(g->gc, value, 0, true);
    if (pyrString == nullptr)
        return errFailed;

    SetObject(slot_this, pyrString);

    return errNone;
}

int prString_Setenv(struct VMGlobals* g, int numArgsPushed) {
    assert(numArgsPushed == 2);
    // String::setEnv {|value| ... }
    PyrSlot* slot_this_name = g->sp - 1;
    PyrSlot* slot_value = g->sp;

    const auto [this_name_err, this_name_str] = slotStdStrVal(slot_this_name);
    if (this_name_err != errNone)
        return this_name_err;

    if (IsNil(slot_value)) {
#ifdef _WIN32
        SetEnvironmentVariableW(SC_Codecvt::utf8_cstr_to_utf16_wstring(this_name_str.c_str()).c_str(), nullptr);
#else
        unsetenv(this_name_str.c_str());
#endif
    } else {
        const auto [value_err, value_str] = slotStdStrVal(slot_value);
        if (value_err != errNone)
            return value_err;
#ifdef _WIN32
        SetEnvironmentVariableW(SC_Codecvt::utf8_cstr_to_utf16_wstring(this_name_str.c_str()).c_str(),
                                SC_Codecvt::utf8_cstr_to_utf16_wstring(value_str.c_str()).c_str());
#else
        setenv(this_name_str.c_str(), value_str.c_str(), 1);
#endif
    }

    return errNone;
}

int prStripRtf(struct VMGlobals* g, int numArgsPushed) {
    PyrSlot* a = g->sp;
    int len = slotRawObject(a)->size;
    char* chars = (char*)malloc(len + 1);
    memcpy(chars, slotRawString(a)->s, len);
    chars[len] = 0;
    rtf2txt(chars);

    PyrString* string = newPyrString(g->gc, chars, 0, false);
    SetObject(a, string);
    free(chars);

    return errNone;
}

int prStripHtml(struct VMGlobals* g, int numArgsPushed) {
    PyrSlot* a = g->sp;
    int len = slotRawObject(a)->size;
    char* chars = (char*)malloc(len + 1);
    memcpy(chars, slotRawString(a)->s, len);
    chars[len] = 0;
    html2txt(chars);

    PyrString* string = newPyrString(g->gc, chars, 0, false);
    SetObject(a, string);
    free(chars);

    return errNone;
}

int prString_Find(struct VMGlobals* g, int numArgsPushed) {
    PyrSlot* a = g->sp - 3; // source string
    PyrSlot* b = g->sp - 2; // search string
    PyrSlot* c = g->sp - 1; // ignoreCase
    PyrSlot* d = g->sp; // offset

    int offset;
    int err = slotIntVal(d, &offset);
    if (err)
        return err;

    int alength = slotRawObject(a)->size - offset;
    if (alength <= 0) {
        SetNil(a);
        return errNone;
    }

    if (isKindOfSlot(b, class_string)) {
        int blength = slotRawObject(b)->size;

        if ((blength == 0)
            // should also return nil if search string is longer than source
            || (blength > alength)) {
            SetNil(a);
            return errNone;
        }

        int cmp = 1; // assume contains will be false
        char* achar = slotRawString(a)->s + offset;
        char* bchar = slotRawString(b)->s;
        char bchar0 = bchar[0];
        int scanlength = alength - blength;
        if (IsTrue(c)) {
            bchar0 = toupper(bchar0);
            for (int i = 0; i <= scanlength; ++i, ++achar) {
                if (toupper(*achar) == bchar0) {
                    cmp = memcmpi(achar + 1, bchar + 1, blength - 1);
                    if (cmp == 0)
                        break;
                }
            }
        } else {
            for (int i = 0; i <= scanlength; ++i, ++achar) {
                if (*achar == bchar0) {
                    cmp = memcmp(achar + 1, bchar + 1, blength - 1);
                    if (cmp == 0)
                        break;
                }
            }
        }
        if (cmp == 0) {
            SetInt(a, achar - slotRawString(a)->s);
        } else {
            SetNil(a);
        }
        return errNone;

    } else if (IsChar(b)) {
        char* achar = slotRawString(a)->s + offset;
        char bchar = slotRawChar(b);
        int scanlength = alength - 1;
        if (IsTrue(c)) {
            bchar = toupper(bchar);
            for (int i = 0; i <= scanlength; ++i, ++achar) {
                if (toupper(*achar) == bchar) {
                    SetInt(a, achar - slotRawString(a)->s);
                    return errNone;
                }
            }
        } else {
            for (int i = 0; i <= scanlength; ++i, ++achar) {
                if (*achar == bchar) {
                    SetInt(a, achar - slotRawString(a)->s);
                    return errNone;
                }
            }
        }
        SetNil(a);
        return errNone;
    } else
        return errWrongType;
}

int prString_FindBackwards(struct VMGlobals* g, int numArgsPushed) {
    PyrSlot* a = g->sp - 3; // source string
    PyrSlot* b = g->sp - 2; // search string
    PyrSlot* c = g->sp - 1; // ignoreCase
    PyrSlot* d = g->sp; // offset

    int offset;
    int err = slotIntVal(d, &offset);
    if (err)
        return err;

    int alength = sc_min(offset + 1, slotRawObject(a)->size);
    if (alength <= 0) {
        SetNil(a);
        return errNone;
    }

    if (isKindOfSlot(b, class_string)) {
        int blength = slotRawObject(b)->size;

        if ((blength == 0)
            // should also return nil if search string is longer than source
            || (blength > alength)) {
            SetNil(a);
            return errNone;
        }

        int cmp = 1; // assume contains will be false
        char* achar = slotRawString(a)->s + (alength - blength);
        char* bchar = slotRawString(b)->s;
        char bchar0 = bchar[0];
        int scanlength = alength - blength;
        if (IsTrue(c)) {
            bchar0 = toupper(bchar0);
            for (int i = scanlength; i >= 0; --i, --achar) {
                if (toupper(*achar) == bchar0) {
                    cmp = memcmpi(achar + 1, bchar + 1, blength - 1);
                    if (cmp == 0)
                        break;
                }
            }
        } else {
            for (int i = scanlength; i >= 0; --i, --achar) {
                if (*achar == bchar0) {
                    cmp = memcmp(achar + 1, bchar + 1, blength - 1);
                    if (cmp == 0)
                        break;
                }
            }
        }
        if (cmp == 0) {
            SetInt(a, achar - slotRawString(a)->s);
        } else {
            SetNil(a);
        }
        return errNone;
    } else if (IsChar(b)) {
        char* achar = slotRawString(a)->s + (alength - 1);
        char bchar = slotRawChar(b);
        int scanlength = alength - 1;
        if (IsTrue(c)) {
            bchar = toupper(bchar);
            for (int i = scanlength; i >= 0; --i, --achar) {
                if (toupper(*achar) == bchar) {
                    SetInt(a, achar - slotRawString(a)->s);
                    return errNone;
                }
            }
        } else {
            for (int i = scanlength; i >= 0; --i, --achar) {
                if (*achar == bchar) {
                    SetInt(a, achar - slotRawString(a)->s);
                    return errNone;
                }
            }
        }
        SetNil(a);
        return errNone;
    } else
        return errWrongType;
}

/** \brief Expand `~` to home directory and resolve aliases
 *
 * Prints an error message if alias resolution failed.
 */
int prString_StandardizePath(struct VMGlobals* g, int /* numArgsPushed */) {
    PyrSlot* arg = g->sp;
    char ipath[PATH_MAX];

    int err = slotStrVal(arg, ipath, PATH_MAX);
    if (err != errNone)
        return err;

    fs::path p = SC_Codecvt::utf8_str_to_path(ipath);
    p = SC_Filesystem::instance().expandTilde(p);
    bool isAlias;
    p = SC_Filesystem::resolveIfAlias(p, isAlias);

    // Don't consider alias resolution a failure condition, but print an error
    if (isAlias && p.empty())
        error("standardizePath: symlink resolution failed for '%s'\n", ipath);

    const std::string& utf8_str = SC_Codecvt::path_to_utf8_str(p);
    PyrString* pyrString = newPyrString(g->gc, utf8_str.c_str(), 0, true);
    SetObject(arg, pyrString);

    return errNone;
}

int prString_EscapeChar(struct VMGlobals* g, int numArgsPushed) {
    PyrSlot* arg = g->sp - 1;
    PyrSlot* charToEscapeSlot = g->sp;

    assert(isKindOfSlot(arg, class_string));

    if (!IsChar(charToEscapeSlot))
        return errWrongType;

    char charToEscape = slotRawChar(charToEscapeSlot);

    PyrString* argString = slotRawString(arg);
    int length = argString->size;
    PyrString* resultString = newPyrStringN(g->gc, length * 2 + 1, 0, 1); // pressimize

    char* original = argString->s;
    char* result = resultString->s;

    int resultLength = length;
    for (int i = 0; i != length; ++i) {
        char current = *original++;
        if (current == charToEscape) {
            *result++ = '\\';
            resultLength += 1;
        }
        *result++ = current;
    }
    *result = 0;

    resultString->size = resultLength;

    SetRaw(arg, (PyrObject*)resultString);

    return errNone;
}

static void yaml_traverse(struct VMGlobals* g, const YAML::Node& node, PyrObject* parent, PyrSlot* slot) {
    YAML::NodeType::value type = node.Type();
    string out;
    PyrObject* result = nullptr;

    switch (type) {
    case YAML::NodeType::Scalar:
        out = node.as<string>();
        result = (PyrObject*)newPyrString(g->gc, out.c_str(), 0, true);
        SetObject(slot, result);
        if (parent)
            g->gc->GCWriteNew(parent, result); // we know result is white so we can use GCWriteNew
        break;

    case YAML::NodeType::Sequence:
        result = newPyrArray(g->gc, node.size(), 0, true);
        SetObject(slot, result);
        if (parent)
            g->gc->GCWriteNew(parent, result); // we know result is white so we can use GCWriteNew
        for (unsigned int i = 0; i < node.size(); i++) {
            const YAML::Node& subnode = node[i];
            result->size++;
            yaml_traverse(g, subnode, result, result->slots + i);
        }
        break;

    case YAML::NodeType::Map: {
        result = instantiateObject(g->gc, s_dictionary->u.classobj, 0, false, true);
        SetObject(slot, result);
        if (parent)
            g->gc->GCWriteNew(parent, result); // we know result is white so we can use GCWriteNew

        PyrObject* array = newPyrArray(g->gc, node.size() * 2, 0, true);
        result->size = 2;
        SetObject(result->slots, array); // array
        SetInt(result->slots + 1, node.size()); // size
        g->gc->GCWriteNew(result, array); // we know array is white so we can use GCWriteNew

        int j = 0;
        for (auto const& element : node) {
            const YAML::Node& key = element.first;
            const YAML::Node& value = element.second;
            out = key.as<string>();
            PyrObject* pkey = (PyrObject*)newPyrString(g->gc, out.c_str(), 0, true);
            SetObject(array->slots + j, pkey);
            array->size++;
            g->gc->GCWriteNew(array, pkey); // we know pkey is white so we can use GCWriteNew

            array->size++;
            yaml_traverse(g, value, array, array->slots + j + 1);

            j += 2;
        }
        break;
    }

    case YAML::NodeType::Null:
        SetNil(slot);
        break;

    default:
        postfl("WARNING: yaml_traverse(): unknown/unsupported node type\n");
        SetNil(slot);
    }
}

int prString_ParseYAML(struct VMGlobals* g, int numArgsPushed) {
    PyrSlot* arg = g->sp;

    if (!isKindOfSlot(arg, class_string))
        return errWrongType;

    string str((const char*)slotRawString(arg)->s, slotRawString(arg)->size);

    std::istringstream fin(str);
    YAML::Node doc = YAML::Load(fin);
    yaml_traverse(g, doc, nullptr, arg);

    return errNone;
}

int prString_ParseYAMLFile(struct VMGlobals* g, int numArgsPushed) {
    PyrSlot* arg = g->sp;

    if (!isKindOfSlot(arg, class_string))
        return errWrongType;

    string str((const char*)slotRawString(arg)->s, slotRawString(arg)->size);

    const fs::path& path = SC_Codecvt::utf8_str_to_path(str);
    std::ifstream fin(path);
    YAML::Node doc = YAML::Load(fin);
    yaml_traverse(g, doc, nullptr, arg);

    return errNone;
}

void initStringPrimitives() {
    int base, index = 0;

    base = nextPrimitiveIndex();

    definePrimitive(base, index++, "_StringCompare", prStringCompare, 3, 0);
    definePrimitive(base, index++, "_StringHash", prStringHash, 1, 0);
    definePrimitive(base, index++, "_StringPathMatch", prString_PathMatch, 1, 0);
    definePrimitive(base, index++, "_StringAsSymbol", prStringAsSymbol, 1, 0);
    definePrimitive(base, index++, "_String_AsInteger", prString_AsInteger, 1, 0);
    definePrimitive(base, index++, "_String_AsFloat", prString_AsFloat, 1, 0);
    definePrimitive(base, index++, "_String_AsCompileString", prString_AsCompileString, 1, 0);
    definePrimitive(base, index++, "_String_Getenv", prString_Getenv, 1, 0);
    definePrimitive(base, index++, "_String_Setenv", prString_Setenv, 2, 0);
    definePrimitive(base, index++, "_String_Find", prString_Find, 4, 0);
    definePrimitive(base, index++, "_String_FindBackwards", prString_FindBackwards, 4, 0);
    definePrimitive(base, index++, "_String_Format", prString_Format, 2, 0);
    definePrimitive(base, index++, "_String_Regexp", prString_Regexp, 4, 0);
    definePrimitive(base, index++, "_String_FindRegexp", prString_FindRegexp, 3, 0);
    definePrimitive(base, index++, "_String_FindRegexpAt", prString_FindRegexpAt, 3, 0);
    definePrimitive(base, index++, "_StripRtf", prStripRtf, 1, 0);
    definePrimitive(base, index++, "_StripHtml", prStripHtml, 1, 0);
    definePrimitive(base, index++, "_String_StandardizePath", prString_StandardizePath, 1, 0);
    definePrimitive(base, index++, "_String_EscapeChar", prString_EscapeChar, 2, 0);
    definePrimitive(base, index++, "_String_ParseYAML", prString_ParseYAML, 1, 0);
    definePrimitive(base, index++, "_String_ParseYAMLFile", prString_ParseYAMLFile, 1, 0);
    definePrimitive(base, index++, "_String_ReplaceRegex", prString_ReplaceRegex, 3, 0);
}
