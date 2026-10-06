#include "auth/validation.h"

#include <algorithm>

namespace solace::auth
{
namespace
{
bool is_form_whitespace(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}
} // namespace

bool is_blank(std::string_view value) noexcept
{
    return std::all_of(value.begin(), value.end(), is_form_whitespace);
}

bool is_valid_email(std::string_view value) noexcept
{
    const std::size_t separator = value.find('@');
    if (separator == std::string_view::npos || separator == 0 ||
        value.find('@', separator + 1) != std::string_view::npos)
        return false;

    for (const char character : value)
        if (is_form_whitespace(character))
            return false;

    const std::string_view domain = value.substr(separator + 1);
    const std::size_t dot = domain.rfind('.');
    return dot != std::string_view::npos && dot > 0 && dot + 1 < domain.size();
}

bool has_minimum_password_length(std::string_view value) noexcept
{
    return value.size() >= minimum_password_length;
}

password_quality evaluate_password(std::string_view value) noexcept
{
    if (!has_minimum_password_length(value))
        return password_quality::too_short;

    int score = static_cast<int>(password_quality::weak);
    if (value.size() >= 12)
        ++score;
    if (value.size() >= 16)
        ++score;

    bool lower = false;
    bool upper = false;
    bool digit = false;
    bool symbol = false;
    for (const char raw_character : value)
    {
        const unsigned char character = static_cast<unsigned char>(raw_character);
        if (character >= 'a' && character <= 'z')
            lower = true;
        else if (character >= 'A' && character <= 'Z')
            upper = true;
        else if (character >= '0' && character <= '9')
            digit = true;
        else
            symbol = true;
    }

    const int classes = static_cast<int>(lower) + static_cast<int>(upper) +
                        static_cast<int>(digit) + static_cast<int>(symbol);
    if (classes >= 3)
        ++score;

    score = (std::min)(score, static_cast<int>(password_quality::strong));
    return static_cast<password_quality>(score);
}
} // namespace solace::auth
