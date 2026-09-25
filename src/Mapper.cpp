#include "Mapper.hpp"

std::string_view toString(Mirroring mirroring) {
    switch (mirroring) {
    case Mirroring::Horizontal: return "Horizontal";
    case Mirroring::Vertical: return "Vertical";
    case Mirroring::FourScreen: return "Four-screen";
    case Mirroring::SingleScreenLower: return "Single-screen (lower)";
    case Mirroring::SingleScreenUpper: return "Single-screen (upper)";
    }
    return "Unknown";
}
