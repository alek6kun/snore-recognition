/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

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
#include <cstring>

#include "feature_provider.h"

// clang-format off
#include "freertos/FreeRTOS.h"
// clang-format on
#include "audio_provider.h"
#include "micro_features_generator.h"
#include "micro_model_settings.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/* From testing, inference takes approximately 450ms to complete. Therefore the
   second core of the microcontroller will listen to 480ms of audio at a time, 
   so that the first core always waits for the second core and the audio data stays
   continuous. */
const int new_samples = 7680; // Number of samples corresponding to 480ms of audio
const int new_features_slices = 30;
const int new_features_elements = new_features_slices * kFeatureSliceSize;
namespace {
  SemaphoreHandle_t xMutex;
  int16_t audio_samples[audio_size] = {0};
  int16_t new_audios_buffer[new_samples] = {0}; // 480 ms of data
  TaskHandle_t xAudio;
}

// Task run by the second core
static void GetAudioSamples(void* arg) {
  while(1) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    xSemaphoreTake(xMutex, portMAX_DELAY);
    capture_sample(new_audios_buffer, new_samples); //defined in audio_provider.cc
    xSemaphoreGive(xMutex);
  }
}

FeatureProvider::FeatureProvider(int feature_size, uint8_t* feature_data)
    : feature_size_(feature_size),
      feature_data_(feature_data),
      is_first_run_(true) {
  // Initialize the feature data to default values.
  for (int n = 0; n < feature_size_; ++n) {
    feature_data_[n] = 0;
  }
}

FeatureProvider::~FeatureProvider() {}

TfLiteStatus FeatureProvider::PopulateFeatureData(
     int32_t last_time_in_ms, int32_t time_in_ms, int* how_many_new_slices) {
  if (feature_size_ != kFeatureElementCount) {
    MicroPrintf("Requested feature_data_ size %d doesn't match %d",
                feature_size_, kFeatureElementCount);
    return kTfLiteError;
  }

  // If this is the first call, initiate the audio sampling task.
  if (is_first_run_) {
    TfLiteStatus init_status = InitializeMicroFeatures();
    xMutex = xSemaphoreCreateMutex();
    xTaskCreate(GetAudioSamples, "GetAudioSamples", 1024 * 8, NULL, 10, &xAudio);
    if (init_status != kTfLiteOk) {
      return init_status;
    }
    is_first_run_ = false;
  }

  *how_many_new_slices = kFeatureSliceCount;

  /*First we shift feature data by 480ms, then we fill new feature data with 480ms of
    audio samples. */
  memmove((void*)audio_samples, (void*)(audio_samples + new_samples),
          sizeof(int16_t)*(audio_size - new_samples));

  /* The feature generator will wait on this line until the audio provider is finished.
     Then it will copy the new samples to a buffer and give the mutex back so the audio
     provider can continue listening right away. */
  xSemaphoreTake(xMutex, portMAX_DELAY);
  memcpy((void*)(audio_samples + audio_size - new_samples),
         (void*)(new_audios_buffer), sizeof(int16_t)*new_samples);
  xSemaphoreGive(xMutex);
  xTaskNotify(xAudio, 0, eNoAction);

  size_t num_samples_read = 0;
  int16_t* new_audio_data = audio_samples + audio_size - new_samples;
  memmove((void*)(feature_data_), (void*)(feature_data_ + new_features_elements),
          sizeof(uint8_t)*(kFeatureElementCount - new_features_elements));
  
  for (int i = 0; i < new_features_slices; ++i) {
    uint8_t* new_slice_data = (feature_data_ + kFeatureElementCount 
        - new_features_elements + (i * kFeatureSliceSize));
    new_audio_data += num_samples_read;
    TfLiteStatus generate_status = GenerateMicroFeatures(
        new_audio_data, audio_size-num_samples_read, kFeatureSliceSize,
        new_slice_data, &num_samples_read);
    if (generate_status != kTfLiteOk) {
      return generate_status;
    }
  }

  return kTfLiteOk;
}
