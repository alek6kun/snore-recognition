/* Copyright 2017 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "recognize_commands.h"

#include <limits>

RecognizeCommands::RecognizeCommands(uint8_t detection_threshold)
      : detection_threshold_(detection_threshold) {}

TfLiteStatus RecognizeCommands::ProcessLatestResults(
    const TfLiteTensor* latest_results, const int32_t current_time_ms,
    const char** found_command, uint8_t* score, bool* is_new_command) {
  if ((latest_results->dims->size != 2) ||
      (latest_results->dims->data[0] != 1)) {
    MicroPrintf(
        "The results for recognition should contain %d elements, but there are "
        "%d in an %d-dimensional shape",
        kCategoryCount, latest_results->dims->data[0],
        latest_results->dims->size);
    return kTfLiteError;
  }

  if (latest_results->type != kTfLiteUInt8) {
    MicroPrintf(
        "The results for recognition should be int8_t elements, but are %d",
        latest_results->type);
    return kTfLiteError;
  }

  bool found = false;
  int16_t new_score = 0;
  uint8_t* scores = latest_results->data.uint8;

  scores_buffer[buffer_index] = scores[0];
  buffer_index++;
  if (buffer_index >= 3 || buffer_index <= -1) 
    buffer_index = 0;
  
  //Calculate the average score across the last 3 results.
  uint8_t buffer_score = (scores_buffer[0] + scores_buffer[1]
    + scores_buffer[2])/3;

  if (buffer_score > detection_threshold_) {
    found = true;
    new_score = buffer_score;
  }

  *is_new_command = found;
  *found_command = kCategoryLabels[0];
  *score = new_score;

  return kTfLiteOk;
}
