#include "fastsm/util/languages.hpp"

namespace fastsm::util {

const std::vector<std::pair<std::string, std::string>>& languages() {
    static const std::vector<std::pair<std::string, std::string>> kLangs = {
        {"en", "English"},   {"es", "Spanish"},    {"fr", "French"},     {"de", "German"},
        {"it", "Italian"},   {"pt", "Portuguese"}, {"nl", "Dutch"},      {"ru", "Russian"},
        {"ja", "Japanese"},  {"zh", "Chinese"},    {"ko", "Korean"},     {"ar", "Arabic"},
        {"hi", "Hindi"},     {"pl", "Polish"},     {"sv", "Swedish"},    {"da", "Danish"},
        {"fi", "Finnish"},   {"no", "Norwegian"},  {"tr", "Turkish"},    {"uk", "Ukrainian"},
        {"cs", "Czech"},     {"el", "Greek"},      {"he", "Hebrew"},     {"id", "Indonesian"},
        {"th", "Thai"},      {"vi", "Vietnamese"},
    };
    return kLangs;
}

} // namespace fastsm::util
