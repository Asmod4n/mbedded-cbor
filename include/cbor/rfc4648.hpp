// Gives the base16 and base64 alphabets and their group sizes in bits
// to the diagnostic notation parser.
// https://www.rfc-editor.org/rfc/rfc4648
#pragma once

#include <string_view>

namespace cbor
{

class rfc4648
{
    static constexpr int base16_group_bits = 4;

    static constexpr int base64_group_bits = 6;

    static constexpr int base64_input_group_bits = 24;

    static constexpr std::string_view base16_alphabet = "0123456789ABCDEF";

    static constexpr std::string_view base64_alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    friend class extended_diagnostic_notation;
};

}
