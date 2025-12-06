#pragma once
#include "api_dto.h"

std::string buildApiResponseJsonString(const ApiResponse& resp);
void printApiResponseJson(const ApiResponse& resp);
