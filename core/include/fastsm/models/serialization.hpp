#pragma once

// JSON (de)serialization for the data models, used by the on-disk timeline
// cache. Declared here so nlohmann/json's ADL finds them wherever the models
// are serialized; defined in serialization.cpp. Enums are stored as integers
// since the cache format is private to the app.

#include <nlohmann/json.hpp>

#include "fastsm/models/models.hpp"

namespace fastsm {

void to_json(nlohmann::json& j, const User& v);
void from_json(const nlohmann::json& j, User& v);

void to_json(nlohmann::json& j, const MediaAttachment& v);
void from_json(const nlohmann::json& j, MediaAttachment& v);

void to_json(nlohmann::json& j, const Mention& v);
void from_json(const nlohmann::json& j, Mention& v);

void to_json(nlohmann::json& j, const Card& v);
void from_json(const nlohmann::json& j, Card& v);

void to_json(nlohmann::json& j, const Poll& v);
void from_json(const nlohmann::json& j, Poll& v);

void to_json(nlohmann::json& j, const Status& v);
void from_json(const nlohmann::json& j, Status& v);

void to_json(nlohmann::json& j, const Notification& v);
void from_json(const nlohmann::json& j, Notification& v);

void to_json(nlohmann::json& j, const TimelineItem& v);
void from_json(const nlohmann::json& j, TimelineItem& v);

} // namespace fastsm
