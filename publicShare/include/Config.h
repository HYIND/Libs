#pragma once

#include <iostream>
#include <fstream>
#include <map>
#include <string>
#include <sstream>

#pragma region ParseIniFile
/*
 * \brief Generic configuration Class
 *
 */

#ifdef _WIN32
#define EXPORT_FUNC __declspec(dllexport)
#elif __linux__
#define EXPORT_FUNC
#endif

class Config
{
    // Instance
public:
    static Config *Instance()
    {
        static Config *m_Instance = new Config("../config/config.ini");
        return m_Instance;
    }

    // Data
protected:
    std::string m_Delimiter;                       //!< separator between key and value
    std::string m_Comment;                         //!< separator between value and comments
    std::map<std::string, std::string> m_Contents; //!< extracted keys and values

    typedef std::map<std::string, std::string>::iterator mapi;
    typedef std::map<std::string, std::string>::const_iterator mapci;
    // Methods
public:
    EXPORT_FUNC Config(std::string filename, std::string delimiter = "=", std::string comment = "#");
    EXPORT_FUNC Config();
    template <class T>
    EXPORT_FUNC T Read(const std::string &in_key) const; //!< Search for key and read value or optional default value, call as read<T>
    template <class T>
    EXPORT_FUNC T Read(const std::string &in_key, const T &in_value) const;
    template <class T>
    EXPORT_FUNC bool ReadInto(T &out_var, const std::string &in_key) const;
    template <class T>
    EXPORT_FUNC bool ReadInto(T &out_var, const std::string &in_key, const T &in_value) const;
    EXPORT_FUNC bool FileExist(std::string filename);
    EXPORT_FUNC void ReadFile(std::string filename, std::string delimiter = "=", std::string comment = "#");

    // Check whether key exists in configuration
    bool KeyExists(const std::string &in_key) const;

    // Modify keys and values
    template <class T>
    void Add(const std::string &in_key, const T &in_value);
    void Remove(const std::string &in_key);

    // Check or change configuration syntax
    std::string GetDelimiter() const { return m_Delimiter; }
    std::string GetComment() const { return m_Comment; }
    std::string SetDelimiter(const std::string &in_s)
    {
        std::string old = m_Delimiter;
        m_Delimiter = in_s;
        return old;
    }
    std::string SetComment(const std::string &in_s)
    {
        std::string old = m_Comment;
        m_Comment = in_s;
        return old;
    }

    // Write or read configuration
    friend std::ostream &operator<<(std::ostream &os, const Config &cf);
    friend std::istream &operator>>(std::istream &is, Config &cf);

protected:
    template <class T>
    static std::string T_as_string(const T &t);
    template <class T>
    static T string_as_T(const std::string &s);

    static void Trim(std::string &inout_s);

    // Exception types
public:
    struct File_not_found
    {
        std::string filename;
        File_not_found(const std::string &filename_ = std::string())
            : filename(filename_) {}
    };
    struct Key_not_found
    { // thrown only by T read(key) variant of read()
        std::string key;
        Key_not_found(const std::string &key_ = std::string())
            : key(key_) {}
    };
};

/* static */
template <class T>
std::string Config::T_as_string(const T &t)
{
    // Convert from a T to a string
    // Type T must support << operator
    std::ostringstream ost;
    ost << t;
    return ost.str();
}

/* static */
template <class T>
T Config::string_as_T(const std::string &s)
{
    // Convert from a string to a T
    // Type T must support >> operator
    T t;
    std::istringstream ist(s);
    ist >> t;
    return t;
}

template <class T>
T Config::Read(const std::string &key) const
{
    // Read the value corresponding to key
    mapci p = m_Contents.find(key);
    if (p == m_Contents.end())
        throw Key_not_found(key);
    return string_as_T<T>(p->second);
}

template <class T>
T Config::Read(const std::string &key, const T &value) const
{
    // Return the value corresponding to key or given default value
    // if key is not found
    mapci p = m_Contents.find(key);
    if (p == m_Contents.end())
        return value;
    return string_as_T<T>(p->second);
}

template <class T>
bool Config::ReadInto(T &var, const std::string &key) const
{
    // Get the value corresponding to key and store in var
    // Return true if key is found
    // Otherwise leave var untouched
    mapci p = m_Contents.find(key);
    bool found = (p != m_Contents.end());
    if (found)
        var = string_as_T<T>(p->second);
    return found;
}

template <class T>
bool Config::ReadInto(T &var, const std::string &key, const T &value) const
{
    // Get the value corresponding to key and store in var
    // Return true if key is found
    // Otherwise set var to given default
    mapci p = m_Contents.find(key);
    bool found = (p != m_Contents.end());
    if (found)
        var = string_as_T<T>(p->second);
    else
        var = value;
    return found;
}

template <class T>
void Config::Add(const std::string &in_key, const T &value)
{
    // Add a key with given value
    std::string v = T_as_string(value);
    std::string key = in_key;
    Trim(key);
    Trim(v);
    m_Contents[key] = v;
    return;
}

#pragma endregion ParseIniFIle
