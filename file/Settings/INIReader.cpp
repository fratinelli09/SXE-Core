
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "ini.h"
#include "INIReader.h"

using std::string;

INIReader::INIReader(const string& filename):
    _filename(filename)
{
    _error = ini_parse(filename.c_str(), ValueHandler, this);
}

INIReader::INIReader(const char *buffer, size_t buffer_size)
{
  _error = ini_parse_string_length(buffer, buffer_size, ValueHandler, this);
}

int INIReader::ParseError() const
{
    return _error;
}

string INIReader::Get(const string& section, const string& name, const string& default_value) const
{
    string key = MakeKey(section, name);
    return _values.count(key) ? _values.find(key)->second : default_value;
}

string INIReader::GetString(const string& section, const string& name, const string& default_value) const
{
    const string str = Get(section, name, "");
    return str.empty() ? default_value : str;
}

long INIReader::GetInteger(const string& section, const string& name, long default_value) const
{
    string valstr = Get(section, name, "");
    const char* value = valstr.c_str();
    char* end;
    // This parses "1234" (decimal) and also "0x4D2" (hex)
    long n = strtol(value, &end, 0);
    return end > value ? n : default_value;
}

INI_API int64_t INIReader::GetInteger64(const string& section, const string& name, int64_t default_value) const
{
    string valstr = Get(section, name, "");
    const char* value = valstr.c_str();
    char* end;
    // This parses "1234" (decimal) and also "0x4D2" (hex)
    int64_t n = strtoll(value, &end, 0);
    return end > value ? n : default_value;
}

unsigned long INIReader::GetUnsigned(const string& section, const string& name, unsigned long default_value) const
{
    string valstr = Get(section, name, "");
    const char* value = valstr.c_str();
    char* end;
    // This parses "1234" (decimal) and also "0x4D2" (hex)
    unsigned long n = strtoul(value, &end, 0);
    return end > value ? n : default_value;
}

INI_API uint64_t INIReader::GetUnsigned64(const string& section, const string& name, uint64_t default_value) const
{
    string valstr = Get(section, name, "");
    const char* value = valstr.c_str();
    char* end;
    // This parses "1234" (decimal) and also "0x4D2" (hex)
    uint64_t n = strtoull(value, &end, 0);
    return end > value ? n : default_value;
}

double INIReader::GetReal(const string& section, const string& name, double default_value) const
{
    string valstr = Get(section, name, "");
    const char* value = valstr.c_str();
    char* end;
    double n = strtod(value, &end);
    return end > value ? n : default_value;
}

bool INIReader::GetBoolean(const string& section, const string& name, bool default_value) const
{
    string valstr = Get(section, name, "");
    // Convert to lower case to make string comparisons case-insensitive
    std::transform(valstr.begin(), valstr.end(), valstr.begin(),
        [](const unsigned char& ch) { return static_cast<unsigned char>(::tolower(ch)); });
    if (valstr == "true" || valstr == "yes" || valstr == "on" || valstr == "1")
        return true;
    else if (valstr == "false" || valstr == "no" || valstr == "off" || valstr == "0")
        return false;
    else
        return default_value;
}

std::vector<string> INIReader::Sections() const
{
    std::set<string> sectionSet;
    for (std::map<string, string>::const_iterator it = _values.begin(); it != _values.end(); ++it) {
        size_t pos = it->first.find('=');
        if (pos != string::npos) {
            sectionSet.insert(it->first.substr(0, pos));
        }
    }
    return std::vector<string>(sectionSet.begin(), sectionSet.end());
}

std::vector<string> INIReader::Keys(const string& section) const
{
    std::vector<string> keys;
    string keyPrefix = MakeKey(section, "");
    for (std::map<string, string>::const_iterator it = _values.begin(); it != _values.end(); ++it) {
        if (it->first.compare(0, keyPrefix.length(), keyPrefix) == 0) {
            keys.push_back(it->first.substr(keyPrefix.length()));
        }
    }
    return keys;
}

bool INIReader::HasSection(const string& section) const
{
    const string key = MakeKey(section, "");
    std::map<string, string>::const_iterator pos = _values.lower_bound(key);
    if (pos == _values.end())
        return false;
    // Does the key at the lower_bound pos start with "section"?
    return pos->first.compare(0, key.length(), key) == 0;
}

bool INIReader::HasValue(const string& section, const string& name) const
{
    string key = MakeKey(section, name);
    return _values.count(key);
}

const string& INIReader::FileName() const
{
    return _filename;
}

// =====================================================
// SCRITTURA
// =====================================================

namespace
{
    const size_t kMaxName    = 49;   // MAX_SECTION / MAX_NAME di ini.cpp meno il terminatore
    const size_t kMaxLine    = 197;  // INI_MAX_LINE meno '\r', '\n' e '\0'

    string ToLower(string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char ch) { return static_cast<char>(::tolower(ch)); });
        return s;
    }

    string Trim(const string& s)
    {
        size_t b = 0, e = s.size();
        while (b < e && isspace(static_cast<unsigned char>(s[b]))) ++b;
        while (e > b && isspace(static_cast<unsigned char>(s[e - 1]))) --e;
        return s.substr(b, e - b);
    }

    // true se il valore, scritto come "name = value", viene riletto identico da ini.cpp
    bool IsValueWritable(const string& value)
    {
        if (value.size() > kMaxLine - 60)                       return false;
        if (value.find_first_of("\r\n") != string::npos)        return false;

        // ';' all'inizio o dopo uno spazio inizierebbe un commento inline
        for (size_t i = 0; i < value.size(); ++i)
            if (value[i] == INI_INLINE_COMMENT_PREFIXES[0] &&
                (i == 0 || isspace(static_cast<unsigned char>(value[i - 1]))))
                return false;
        return true;
    }

    bool IsNameWritable(const string& name)
    {
        return !name.empty() && name.size() <= kMaxName &&
               name == Trim(name) &&
               name.find_first_of("=:[]\r\n") == string::npos &&
               !strchr(INI_START_COMMENT_PREFIXES, name[0]);
    }

    bool IsSectionWritable(const string& section)
    {
        return section.size() <= kMaxName &&
               section.find_first_of("]\r\n") == string::npos &&
               section == Trim(section);
    }

    enum LineKind { kBlank, kComment, kSection, kKey, kContinuation, kOther };

    struct LineInfo
    {
        LineKind kind = kBlank;
        string   section;   // per kSection
        string   name;      // per kKey
    };

    // Classifica una riga con le stesse regole di ini_parse_stream
    LineInfo Classify(const string& line, string& prevName)
    {
        LineInfo info;

        size_t b = 0;
        while (b < line.size() && isspace(static_cast<unsigned char>(line[b]))) ++b;
        const string start = Trim(line);

        if (start.empty())
        {
            info.kind = kBlank;
            return info;
        }
        if (strchr(INI_START_COMMENT_PREFIXES, start[0]))
        {
            info.kind = kComment;
            return info;
        }
        if (!prevName.empty() && b > 0)
        {
            info.kind = kContinuation;
            info.name = prevName;
            return info;
        }
        if (start[0] == '[')
        {
            const size_t end = start.find(']');
            if (end == string::npos)
            {
                info.kind = kOther;
                return info;
            }
            info.kind    = kSection;
            info.section = start.substr(1, end - 1);
            prevName.clear();
            return info;
        }

        const size_t sep = start.find_first_of("=:");
        if (sep == string::npos)
        {
            info.kind = kOther;
            return info;
        }
        info.kind = kKey;
        info.name = Trim(start.substr(0, sep));
        prevName  = info.name;
        return info;
    }
}

bool INIReader::Set(const string& section, const string& name, const string& value)
{
    if (_filename.empty())
        return false;

    const string strValue = Trim(value);
    if (!IsSectionWritable(section) || !IsNameWritable(name) || !IsValueWritable(strValue))
        return false;

    const string strNewLine = name + " = " + strValue;
    if (strNewLine.size() > kMaxLine)
        return false;

    // ---- lettura del file (se esiste) ----
    std::vector<string> lines;
    bool   bBom = false;
    string strEol = "\n";

    {
        std::ifstream in(_filename, std::ios::binary);
        if (in)
        {
            std::stringstream buffer;
            buffer << in.rdbuf();
            string content = buffer.str();

            if (content.compare(0, 3, "\xEF\xBB\xBF") == 0)
            {
                bBom = true;
                content.erase(0, 3);
            }
            if (content.find("\r\n") != string::npos)
                strEol = "\r\n";

            std::stringstream lineStream(content);
            string line;
            while (std::getline(lineStream, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                lines.push_back(line);
            }
        }
    }

    // ---- ricerca della chiave e della posizione di inserimento ----
    const string strSectionLow = ToLower(section);
    const string strNameLow    = ToLower(name);

    string curSection;
    string prevName;

    struct Range { size_t first; size_t last; };
    std::vector<Range> found;                 // occorrenze della chiave (riga + continuazioni)

    bool   bSectionFound   = strSectionLow.empty();   // la sezione "" esiste sempre
    size_t nInsertAt       = 0;                       // dopo l'ultima riga utile della sezione
    bool   bInsertDefined  = false;
    size_t nFirstHeader    = lines.size();

    for (size_t i = 0; i < lines.size(); ++i)
    {
        const LineInfo info = Classify(lines[i], prevName);

        if (info.kind == kSection)
        {
            curSection = ToLower(info.section);
            if (nFirstHeader == lines.size())
                nFirstHeader = i;
            if (curSection == strSectionLow)
            {
                bSectionFound  = true;
                nInsertAt      = i + 1;
                bInsertDefined = true;
            }
            continue;
        }

        if (curSection != strSectionLow)
            continue;

        if (info.kind == kKey || info.kind == kContinuation)
        {
            nInsertAt      = i + 1;
            bInsertDefined = true;

            if (ToLower(info.name) == strNameLow)
            {
                if (info.kind == kKey)
                    found.push_back({i, i});
                else if (!found.empty())
                    found.back().last = i;
            }
        }
    }

    // ---- modifica delle righe ----
    if (!found.empty())
    {
        // si sostituisce la prima occorrenza e si eliminano le eventuali duplicate
        for (size_t k = found.size(); k-- > 1; )
            lines.erase(lines.begin() + found[k].first, lines.begin() + found[k].last + 1);

        lines.erase(lines.begin() + found[0].first + 1, lines.begin() + found[0].last + 1);
        lines[found[0].first] = strNewLine;
    }
    else if (bSectionFound && strSectionLow.empty() && !bInsertDefined)
    {
        // chiave senza sezione e nessuna chiave senza sezione presente: va prima del primo [header]
        lines.insert(lines.begin() + nFirstHeader, strNewLine);
        if (nFirstHeader + 1 < lines.size())
            lines.insert(lines.begin() + nFirstHeader + 1, "");
    }
    else if (bSectionFound)
    {
        lines.insert(lines.begin() + nInsertAt, strNewLine);
    }
    else
    {
        if (!lines.empty() && !Trim(lines.back()).empty())
            lines.push_back("");
        lines.push_back("[" + section + "]");
        lines.push_back(strNewLine);
    }

    // ---- scrittura atomica: file temporaneo + rename ----
    const string strTmp = _filename + ".tmp";
    {
        std::ofstream out(strTmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;

        if (bBom)
            out << "\xEF\xBB\xBF";
        for (const string& line : lines)
            out << line << strEol;

        out.flush();
        if (!out)
        {
            out.close();
            std::error_code ec;
            std::filesystem::remove(strTmp, ec);
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::rename(strTmp, _filename, ec);
    if (ec)
    {
        std::filesystem::remove(strTmp, ec);
        return false;
    }

    // ---- copia in memoria allineata al file ----
    _values[MakeKey(section, name)] = strValue;
    if (_error == -1)           // il file non esisteva: ora c'e'
        _error = 0;

    return true;
}

string INIReader::MakeKey(const string& section, const string& name)
{
    string key = section + "=" + name;
    // Convert to lower case to make section/name lookups case-insensitive
    std::transform(key.begin(), key.end(), key.begin(),
        [](const unsigned char& ch) { return static_cast<unsigned char>(::tolower(ch)); });
    return key;
}

int INIReader::ValueHandler(void* user, const char* section, const char* name,
                            const char* value)
{
    if (!name)  // Happens when INI_CALL_HANDLER_ON_NEW_SECTION enabled
        return 1;
    INIReader* reader = static_cast<INIReader*>(user);
    string key = MakeKey(section, name);
    if (reader->_values[key].size() > 0)
        reader->_values[key] += "\n";
    reader->_values[key] += value ? value : "";
    return 1;
}
