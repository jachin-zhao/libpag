/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//

#pragma once

#include "platform/Platform.h"

namespace pag {
class NativePlatform : public Platform {
 public:
  std::vector<const VideoDecoderFactory*> getVideoDecoderFactories() const override;

  bool registerFallbackFonts() const override;

  void traceImage(const tgfx::ImageInfo& info, const void* pixels,
                  const std::string& tag) const override;

  std::string getCacheDir() const override;

  std::shared_ptr<DisplayLink> createDisplayLink(std::function<void()> callback) const override;
};
}  // namespace pag
