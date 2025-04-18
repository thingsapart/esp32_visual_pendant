#ifdef POSIX

#include <string>       // std::string - the internal workhorse
#include <vector>       // For getBytes buffer
#include <algorithm>    // std::transform, std::find_if_not, std::equal, std::replace
#include <cctype>       // ::tolower, ::toupper, ::isspace
#include <cstring>      // std::strlen, std::memcpy, std::strcmp
#include <sstream>      // std::stringstream for number formatting
#include <iomanip>      // std::setprecision, std::fixed, std::hex, std::oct
#include <stdexcept>    // For stoX conversion error handling (optional but good practice)
#include <limits>       // std::numeric_limits (for remove default)
#include <iostream>     // For std::ostream support

// Forward declaration for friend functions
class ArduinoString;

// Non-member operator+ declarations (needed before class for some compilers/orders)
ArduinoString operator+(const ArduinoString& lhs, const ArduinoString& rhs);
ArduinoString operator+(const ArduinoString& lhs, const char* rhs);
ArduinoString operator+(const char* lhs, const ArduinoString& rhs);
ArduinoString operator+(const ArduinoString& lhs, char rhs);
ArduinoString operator+(char lhs, const ArduinoString& rhs);
// Add declarations for numbers if needed, e.g.:
// ArduinoString operator+(const ArduinoString& lhs, int rhs);
// ArduinoString operator+(int lhs, const ArduinoString& rhs); // etc.

class ArduinoString {
private:
    std::string _data; // Internal storage

    // --- Private Helpers ---

    // Helper to format integers (unsigned long covers most Arduino int types)
    static std::string integerToString(unsigned long long value, int base) {
        if (base < 2 || base > 36) base = 10; // Default to base 10 if invalid
        if (value == 0) return "0";

        // Special handling for common bases via std::stringstream
        if (base == 10) return std::to_string(value); // Use std::to_string for base 10 unsigned long long
        if (base == 16 || base == 8 || base == 2) { // Stringstream for HEX, OCT, BIN
             std::stringstream ss;
             if (base == 16) ss << std::hex << value;
             else if (base == 8) ss << std::oct << value;
             else if (base == 2) { // Manual binary for std::stringstream deficiency
                 std::string binStr;
                 do { binStr += ((value % 2) ? '1' : '0'); value /= 2; } while (value > 0);
                 std::reverse(binStr.begin(), binStr.end());
                 return binStr;
             }
             return ss.str();
        }

        // Manual conversion for other bases (less common for Arduino String)
        std::string result = "";
        const char* digits = "0123456789abcdefghijklmnopqrstuvwxyz";
        while (value > 0) {
            result += digits[value % base];
            value /= base;
        }
        std::reverse(result.begin(), result.end());
        return result;
    }

     // Helper for signed integers
     static std::string signedIntegerToString(long long value, int base) {
        if (value < 0 && base == 10) { // Only handle negative for base 10 easily
            return "-" + integerToString(static_cast<unsigned long long>(-value), 10);
        }
        // For other bases, treat as unsigned (like Arduino often does implicitly)
        // or handle 2's complement if strictness is needed (more complex)
        return integerToString(static_cast<unsigned long long>(value), base);
     }


    // Helper to format floating-point numbers
    static std::string floatToString(double val, unsigned int decimalPlaces) {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(decimalPlaces) << val;
        return ss.str();
    }

public:
    // --- Constructors ---
    ArduinoString() = default; // Creates an empty string ""
    ArduinoString(const char* cstr) : _data(cstr ? cstr : "") {}
    ArduinoString(const std::string& s) : _data(s) {} // Allow construction from std::string
    ArduinoString(const ArduinoString& other) : _data(other._data) {}
    ArduinoString(char c) : _data(1, c) {}
    ArduinoString(unsigned char value, int base = 10) : _data(integerToString(value, base)) {}
    ArduinoString(int value, int base = 10) : _data(signedIntegerToString(value, base)) {}
    ArduinoString(unsigned int value, int base = 10) : _data(integerToString(value, base)) {}
    ArduinoString(long value, int base = 10) : _data(signedIntegerToString(value, base)) {}
    ArduinoString(unsigned long value, int base = 10) : _data(integerToString(value, base)) {}
    // Add long long / unsigned long long if needed for newer Arduino cores/platforms
    ArduinoString(float value, unsigned int decimalPlaces = 2) : _data(floatToString(value, decimalPlaces)) {}
    ArduinoString(double value, unsigned int decimalPlaces = 2) : _data(floatToString(value, decimalPlaces)) {}

    // Allow explicit conversion from std::string
    explicit ArduinoString(std::string&& s) : _data(std::move(s)) {} // Move constructor


    // --- Assignment ---
    ArduinoString& operator=(const ArduinoString& rhs) { _data = rhs._data; return *this; }
    ArduinoString& operator=(const char* cstr) { _data = (cstr ? cstr : ""); return *this; }
    ArduinoString& operator=(const std::string& rhs) { _data = rhs; return *this; }
    ArduinoString& operator=(char c) { _data = c; return *this; }
    // Assignment from numbers delegates to constructors + assignment
    ArduinoString& operator=(unsigned char value) { *this = ArduinoString(value); return *this; }
    ArduinoString& operator=(int value) { *this = ArduinoString(value); return *this; }
    ArduinoString& operator=(unsigned int value) { *this = ArduinoString(value); return *this; }
    ArduinoString& operator=(long value) { *this = ArduinoString(value); return *this; }
    ArduinoString& operator=(unsigned long value) { *this = ArduinoString(value); return *this; }
    ArduinoString& operator=(float value) { *this = ArduinoString(value); return *this; }
    ArduinoString& operator=(double value) { *this = ArduinoString(value); return *this; }

    // Move assignment
    ArduinoString& operator=(ArduinoString&& rhs) noexcept { _data = std::move(rhs._data); return *this; }
    ArduinoString& operator=(std::string&& rhs) noexcept { _data = std::move(rhs); return *this; }


    // --- Concatenation ---
    ArduinoString& operator+=(const ArduinoString& rhs) { _data += rhs._data; return *this; }
    ArduinoString& operator+=(const char* cstr) { if (cstr) _data += cstr; return *this; }
    ArduinoString& operator+=(char c) { _data += c; return *this; }
    ArduinoString& operator+=(unsigned char val) { return (*this += ArduinoString(val)); }
    ArduinoString& operator+=(int val) { return (*this += ArduinoString(val)); }
    ArduinoString& operator+=(unsigned int val) { return (*this += ArduinoString(val)); }
    ArduinoString& operator+=(long val) { return (*this += ArduinoString(val)); }
    ArduinoString& operator+=(unsigned long val) { return (*this += ArduinoString(val)); }
    ArduinoString& operator+=(float val) { return (*this += ArduinoString(val)); }
    ArduinoString& operator+=(double val) { return (*this += ArduinoString(val)); }

    // concat() method variants - return true mirroring Arduino (failure unlikely with std::string)
    bool concat(const ArduinoString& s) { _data += s._data; return true; }
    bool concat(const char* cstr) { if (cstr) _data += cstr; return true; }
    bool concat(char c) { _data += c; return true; }
    bool concat(unsigned char val) { return concat(ArduinoString(val)); }
    bool concat(int val) { return concat(ArduinoString(val)); }
    bool concat(unsigned int val) { return concat(ArduinoString(val)); }
    bool concat(long val) { return concat(ArduinoString(val)); }
    bool concat(unsigned long val) { return concat(ArduinoString(val)); }
    bool concat(float val) { return concat(ArduinoString(val)); }
    bool concat(double val) { return concat(ArduinoString(val)); }


    // --- Accessors ---
    unsigned int length() const { return static_cast<unsigned int>(_data.length()); }
    const char* c_str() const { return _data.c_str(); } // Identical
    // Arduino String operator[] returns char&, allowing modification
    char& operator[](unsigned int index) { return _data[index]; }
    // Arduino String const operator[] returns char by value (copy)
    char operator[](unsigned int index) const { return _data[index]; }
    // charAt is bounds-checked and returns '\0' if out of range
    char charAt(unsigned int index) const { return (index < length()) ? _data[index] : '\0'; }
    void setCharAt(unsigned int index, char c) { if (index < length()) _data[index] = c; }

    // Copy substring into C-style buffer
    void getBytes(unsigned char* buf, unsigned int bufsize, unsigned int index = 0) const {
        if (!buf || bufsize == 0) return;
        unsigned int len = length();
        if (index >= len) { // If index is out of bounds, terminate buffer
            buf[0] = '\0';
            return;
        }
        // Number of chars to copy: minimum of remaining chars and (bufsize-1 for null term)
        unsigned int count = std::min(len - index, bufsize - 1);
        std::memcpy(buf, _data.c_str() + index, count);
        buf[count] = '\0'; // Null terminate
    }
    // toCharArray is equivalent to getBytes for char buffers
    void toCharArray(char* buf, unsigned int bufsize, unsigned int index = 0) const {
         getBytes(reinterpret_cast<unsigned char*>(buf), bufsize, index);
    }


    // --- Comparisons ---
    int compareTo(const ArduinoString& s) const { return _data.compare(s._data); }
    int compareTo(const char* cstr) const { return _data.compare(cstr ? cstr : ""); }

    bool equals(const ArduinoString& s) const { return _data == s._data; }
    bool equals(const char* cstr) const { return _data == (cstr ? cstr : ""); }

    bool equalsIgnoreCase(const ArduinoString& s) const {
        if (length() != s.length()) return false;
        return std::equal(_data.begin(), _data.end(), s._data.begin(),
                          [](char a, char b) { return ::tolower(static_cast<unsigned char>(a)) == ::tolower(static_cast<unsigned char>(b)); });
    }
    bool equalsIgnoreCase(const char* cstr) const {
        if (!cstr) return _data.empty(); // Match Arduino: s.equalsIgnoreCase(NULL) is true iff s is empty
        size_t cstr_len = std::strlen(cstr);
        if (length() != cstr_len) return false;
         return std::equal(_data.begin(), _data.end(), cstr,
                          [](char a, char b) { return ::tolower(static_cast<unsigned char>(a)) == ::tolower(static_cast<unsigned char>(b)); });
    }

    // Comparison operators
    bool operator==(const ArduinoString& rhs) const { return _data == rhs._data; }
    bool operator==(const char* cstr) const { return _data == (cstr ? cstr : ""); }
    bool operator!=(const ArduinoString& rhs) const { return _data != rhs._data; }
    bool operator!=(const char* cstr) const { return _data != (cstr ? cstr : ""); }
    bool operator<(const ArduinoString& rhs) const { return _data < rhs._data; }
    bool operator<(const char* cstr) const { return _data < (cstr ? cstr : ""); }
    bool operator>(const ArduinoString& rhs) const { return _data > rhs._data; }
    bool operator>(const char* cstr) const { return _data > (cstr ? cstr : ""); }
    bool operator<=(const ArduinoString& rhs) const { return _data <= rhs._data; }
    bool operator<=(const char* cstr) const { return _data <= (cstr ? cstr : ""); }
    bool operator>=(const ArduinoString& rhs) const { return _data >= rhs._data; }
    bool operator>=(const char* cstr) const { return _data >= (cstr ? cstr : ""); }


    // --- Search ---
    // Note: std::string::find returns std::string::npos on failure, Arduino returns -1
    int indexOf(char ch, unsigned int fromIndex = 0) const {
        size_t pos = _data.find(ch, fromIndex);
        return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
    }
    int indexOf(const ArduinoString& str, unsigned int fromIndex = 0) const {
        size_t pos = _data.find(str._data, fromIndex);
        return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
    }
     int indexOf(const char* cstr, unsigned int fromIndex = 0) const {
        if (!cstr) return -1; // Arduino behavior for null differs, often finds ""
        size_t pos = _data.find(cstr, fromIndex);
        return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
    }

    // Arduino uses signed int for fromIndex, std::string::rfind uses size_t.
    // Default fromIndex=-1 means search from the end.
    int lastIndexOf(char ch, int fromIndex = -1) const {
        size_t startPos = (fromIndex < 0 || static_cast<unsigned int>(fromIndex) >= length())
                          ? std::string::npos // Search from end if invalid or default
                          : static_cast<size_t>(fromIndex);
        size_t pos = _data.rfind(ch, startPos);
        return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
    }
    int lastIndexOf(const ArduinoString& str, int fromIndex = -1) const {
         size_t startPos = (fromIndex < 0 || static_cast<unsigned int>(fromIndex) >= length())
                           ? std::string::npos
                           : static_cast<size_t>(fromIndex);
        size_t pos = _data.rfind(str._data, startPos);
        return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
    }
     int lastIndexOf(const char* cstr, int fromIndex = -1) const {
        if (!cstr) return -1;
        size_t startPos = (fromIndex < 0 || static_cast<unsigned int>(fromIndex) >= length())
                           ? std::string::npos
                           : static_cast<size_t>(fromIndex);
        size_t pos = _data.rfind(cstr, startPos);
        return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
    }

    bool startsWith(const ArduinoString& prefix) const {
        return _data.rfind(prefix._data, 0) == 0; // Efficient check using rfind
    }
    bool startsWith(const char* prefix) const {
        return prefix && (_data.rfind(prefix, 0) == 0);
    }
    // Arduino allows offset for startsWith
    bool startsWith(const ArduinoString& prefix, unsigned int offset) const {
        if (offset >= length()) return prefix.length() == 0; // Match Arduino edge case
        return _data.rfind(prefix._data, offset) == offset;
    }
     bool startsWith(const char* prefix, unsigned int offset) const {
        if (!prefix) return false;
        if (offset >= length()) return (*prefix == '\0'); // Match Arduino edge case
        return _data.rfind(prefix, offset) == offset;
    }

    bool endsWith(const ArduinoString& suffix) const {
        if (suffix.length() > length()) return false;
        return _data.compare(length() - suffix.length(), suffix.length(), suffix._data) == 0;
    }
     bool endsWith(const char* suffix) const {
        if (!suffix) return false; // Arduino seems to return false for null suffix
        size_t suffixLen = std::strlen(suffix);
        if (suffixLen > length()) return false;
        return _data.compare(length() - suffixLen, suffixLen, suffix) == 0;
    }


    // --- Modification ---
    // Arduino substring(beginIndex) goes to end
    ArduinoString substring(unsigned int beginIndex) const {
        if (beginIndex >= length()) return ArduinoString(); // Return empty string if out of bounds
        return ArduinoString(_data.substr(beginIndex));
    }
    // Arduino substring(beginIndex, endIndex): endIndex is *exclusive*
    // std::string substr(pos, count): count is *length*
    ArduinoString substring(unsigned int beginIndex, unsigned int endIndex) const {
        if (beginIndex >= length()) return ArduinoString(); // Empty if start out of bounds
        if (beginIndex >= endIndex) return ArduinoString(); // Empty if end <= start
        // Clamp endIndex to string length
        unsigned int realEndIndex = std::min(endIndex, length());
        unsigned int count = realEndIndex - beginIndex;
        return ArduinoString(_data.substr(beginIndex, count));
    }

    void replace(char find, char replaceChar) {
        std::replace(_data.begin(), _data.end(), find, replaceChar);
    }
    // Replace all occurrences
    void replace(const ArduinoString& find, const ArduinoString& replaceWith) {
         if (find.length() == 0) return; // Avoid infinite loop if find is empty
         size_t start_pos = 0;
         while((start_pos = _data.find(find._data, start_pos)) != std::string::npos) {
             _data.replace(start_pos, find.length(), replaceWith._data);
             // Move past the replaced section
             start_pos += replaceWith.length();
         }
    }
    void replace(const char* find, const char* replaceWith) {
        if (!find || !replaceWith) return;
        replace(ArduinoString(find), ArduinoString(replaceWith));
    }

    // Arduino remove: count=0 means remove nothing, default count removes to end
    void remove(unsigned int index, unsigned int count = std::numeric_limits<unsigned int>::max()) {
       if (index >= length() || count == 0) return; // Nothing to remove
       // Calculate actual count to remove, preventing overflow/over-erase
       unsigned int max_possible_count = length() - index;
       unsigned int actual_count = std::min(count, max_possible_count);
       _data.erase(index, actual_count);
    }

    void toLowerCase() {
        std::transform(_data.begin(), _data.end(), _data.begin(),
                       [](unsigned char c){ return ::tolower(c); });
    }
    void toUpperCase() {
        std::transform(_data.begin(), _data.end(), _data.begin(),
                       [](unsigned char c){ return ::toupper(c); });
    }

    void trim() {
        // Find the first non-whitespace character
        auto first = std::find_if_not(_data.begin(), _data.end(), [](unsigned char c){ return ::isspace(c); });
        // Find the last non-whitespace character (searching from the end)
        auto last = std::find_if_not(_data.rbegin(), _data.rend(), [](unsigned char c){ return ::isspace(c); }).base();
        // Assign the substring between first and last
        _data = (first < last) ? std::string(first, last) : "";
    }

    // --- Capacity ---
    void reserve(unsigned int size) { _data.reserve(size); }
    // Arduino's invalidate() is meant to free buffer; std::string::clear() removes content
    // but doesn't guarantee immediate memory release. std::string::shrink_to_fit() is closer.
    void invalidate() {
        _data.clear();
        _data.shrink_to_fit(); // Request memory release (C++11)
    }
    // Provide clear() as the standard C++ equivalent
    void clear() {
        _data.clear();
    }


    // --- Conversions ---
    // Mimic Arduino's behavior of returning 0 on conversion failure
    long toInt() const {
        try {
            // Use strtol for better C compatibility and error checking style
            char* endptr;
            long val = std::strtol(_data.c_str(), &endptr, 10);
            // Arduino String allows trailing non-numeric chars, check if *any* conversion happened
            return (endptr == _data.c_str()) ? 0 : val;
        } catch (...) { // Catch potential exceptions from std::strtol if used differently, though less likely
            return 0;
        }
    }
    float toFloat() const {
         try {
            char* endptr;
            float val = std::strtof(_data.c_str(), &endptr);
            return (endptr == _data.c_str()) ? 0.0f : val;
        } catch (...) {
            return 0.0f;
        }
    }
     double toDouble() const { // Add double if needed (often same as float on Arduino)
         try {
            char* endptr;
            double val = std::strtod(_data.c_str(), &endptr);
            return (endptr == _data.c_str()) ? 0.0 : val;
        } catch (...) {
            return 0.0;
        }
    }

    // --- Non-member Friends for operators ---
    friend ArduinoString operator+(const ArduinoString& lhs, const ArduinoString& rhs) {
        ArduinoString result(lhs); result += rhs; return result;
    }
    friend ArduinoString operator+(const ArduinoString& lhs, const char* rhs) {
        ArduinoString result(lhs); result += rhs; return result;
    }
    friend ArduinoString operator+(const char* lhs, const ArduinoString& rhs) {
        ArduinoString result(lhs); result += rhs; return result;
    }
    friend ArduinoString operator+(const ArduinoString& lhs, char rhs) {
        ArduinoString result(lhs); result += rhs; return result;
    }
    friend ArduinoString operator+(char lhs, const ArduinoString& rhs) {
        ArduinoString result(lhs); result += rhs; return result;
    }
    // Add friends for numbers if needed (can often rely on += and constructors)
    // Example:
    friend ArduinoString operator+(const ArduinoString& lhs, int rhs) {
         ArduinoString result(lhs); result += rhs; return result;
    }
     friend ArduinoString operator+(int lhs, const ArduinoString& rhs) {
         ArduinoString result(lhs); result += rhs; return result;
     }
     // ... other numeric types


    // Make std::ostream compatible (e.g., for std::cout)
    friend std::ostream& operator<<(std::ostream& os, const ArduinoString& s) {
        os << s._data;
        return os;
    }

    // Allow conversion *to* std::string (use cautiously)
    // explicit operator std::string() const { return _data; } // Explicit prevents unwanted implicit conversions
     std::string toString() const { return _data; } // Safer alternative: explicit method

};

// Define symmetric comparison operators for const char* on LHS
inline bool operator==(const char* lhs, const ArduinoString& rhs) { return rhs == lhs; }
inline bool operator!=(const char* lhs, const ArduinoString& rhs) { return rhs != lhs; }
inline bool operator<(const char* lhs, const ArduinoString& rhs) { return rhs > lhs; }
inline bool operator>(const char* lhs, const ArduinoString& rhs) { return rhs < lhs; }
inline bool operator<=(const char* lhs, const ArduinoString& rhs) { return rhs >= lhs; }
inline bool operator>=(const char* lhs, const ArduinoString& rhs) { return rhs <= lhs; }

#endif