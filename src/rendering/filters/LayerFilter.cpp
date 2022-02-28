/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
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
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "LayerFilter.h"
#include "BulgeFilter.h"
#include "CornerPinFilter.h"
#include "DisplacementMapFilter.h"
#include "LevelsIndividualFilter.h"
#include "MosaicFilter.h"
#include "MotionTileFilter.h"
#include "RadialBlurFilter.h"
#include "rendering/filters/dropshadow/DropShadowFilter.h"
#include "rendering/filters/gaussblur/GaussBlurFilter.h"
#include "rendering/filters/glow/GlowFilter.h"
#include "rendering/filters/utils/FilterHelper.h"

namespace pag {
static constexpr char VERTEX_SHADER[] = R"(
    #version 100
    attribute vec2 aPosition;
    attribute vec2 aTextureCoord;
    uniform mat3 uVertexMatrix;
    uniform mat3 uTextureMatrix;
    varying vec2 vertexColor;
    void main() {
    vec3 position = uVertexMatrix * vec3(aPosition, 1);
    gl_Position = vec4(position.xy, 0, 1);
    vec3 colorPosition = uTextureMatrix * vec3(aTextureCoord, 1);
    vertexColor = colorPosition.xy;
    }
)";

static constexpr char FRAGMENT_SHADER[] = R"(
    #version 100
    precision mediump float;
    varying vec2 vertexColor;
    uniform sampler2D sTexture;

    void main() {
        gl_FragColor = texture2D(sTexture, vertexColor);
    }
)";

std::vector<tgfx::Point> ComputeVerticesForMotionBlurAndBulge(const tgfx::Rect& inputBounds,
                                                              const tgfx::Rect& outputBounds) {
  std::vector<tgfx::Point> vertices = {};
  tgfx::Point contentPoint[4] = {{outputBounds.left, outputBounds.bottom},
                                 {outputBounds.right, outputBounds.bottom},
                                 {outputBounds.left, outputBounds.top},
                                 {outputBounds.right, outputBounds.top}};
  auto deltaX = outputBounds.left - inputBounds.left;
  auto deltaY = outputBounds.top - inputBounds.top;
  tgfx::Point texturePoints[4] = {
      {deltaX, (outputBounds.height() + deltaY)},
      {(outputBounds.width() + deltaX), (outputBounds.height() + deltaY)},
      {deltaX, deltaY},
      {(outputBounds.width() + deltaX), deltaY}};
  for (int ii = 0; ii < 4; ii++) {
    vertices.push_back(contentPoint[ii]);
    vertices.push_back(texturePoints[ii]);
  }
  return vertices;
}

std::shared_ptr<const FilterProgram> FilterProgram::Make(tgfx::Context* context,
                                                         const std::string& vertex,
                                                         const std::string& fragment) {
  auto gl = tgfx::GLInterface::Get(context);
  auto program = tgfx::CreateGLProgram(gl, vertex, fragment);
  if (program == 0) {
    return nullptr;
  }
  auto filterProgram = new FilterProgram();
  filterProgram->program = program;
  if (gl->caps->vertexArrayObjectSupport) {
    gl->functions->genVertexArrays(1, &filterProgram->vertexArray);
  }
  gl->functions->genBuffers(1, &filterProgram->vertexBuffer);
  return Resource::Wrap(context, filterProgram);
}

void FilterProgram::onRelease(tgfx::Context* context) {
  auto gl = tgfx::GLInterface::Get(context);
  if (program > 0) {
    gl->functions->deleteProgram(program);
    program = 0;
  }
  if (vertexArray > 0) {
    gl->functions->deleteVertexArrays(1, &vertexArray);
    vertexArray = 0;
  }
  if (vertexBuffer > 0) {
    gl->functions->deleteBuffers(1, &vertexBuffer);
    vertexBuffer = 0;
  }
}

std::unique_ptr<LayerFilter> LayerFilter::Make(LayerStyle* layerStyle) {
  LayerFilter* filter = nullptr;
  switch (layerStyle->type()) {
    case LayerStyleType::DropShadow:
      filter = new DropShadowFilter(reinterpret_cast<DropShadowStyle*>(layerStyle));
    default:
      break;
  }
  return std::unique_ptr<LayerFilter>(filter);
}

std::unique_ptr<LayerFilter> LayerFilter::Make(Effect* effect) {
  LayerFilter* filter = nullptr;
  switch (effect->type()) {
    case EffectType::CornerPin:
      filter = new CornerPinFilter(effect);
      break;
    case EffectType::Bulge:
      filter = new BulgeFilter(effect);
      break;
    case EffectType::MotionTile:
      filter = new MotionTileFilter(effect);
      break;
    case EffectType::Glow:
      filter = new GlowFilter(effect);
      break;
    case EffectType::LevelsIndividual:
      filter = new LevelsIndividualFilter(effect);
      break;
    case EffectType::FastBlur:
      filter = new GaussBlurFilter(effect);
      break;
    case EffectType::DisplacementMap:
      filter = new DisplacementMapFilter(effect);
      break;
    case EffectType::RadialBlur:
      filter = new RadialBlurFilter(effect);
      break;
    case EffectType::Mosaic:
      filter = new MosaicFilter(effect);
      break;
    default:
      break;
  }
  return std::unique_ptr<LayerFilter>(filter);
}

bool LayerFilter::initialize(tgfx::Context* context) {
  auto gl = tgfx::GLInterface::Get(context);
  // 防止前面产生的GLError，导致后面CheckGLError逻辑返回错误结果
  CheckGLError(gl);

  auto vertex = onBuildVertexShader();
  auto fragment = onBuildFragmentShader();
  filterProgram = FilterProgram::Make(context, vertex, fragment);
  if (filterProgram == nullptr) {
    return false;
  }
  auto program = filterProgram->program;
  positionHandle = gl->functions->getAttribLocation(program, "aPosition");
  textureCoordHandle = gl->functions->getAttribLocation(program, "aTextureCoord");
  vertexMatrixHandle = gl->functions->getUniformLocation(program, "uVertexMatrix");
  textureMatrixHandle = gl->functions->getUniformLocation(program, "uTextureMatrix");
  onPrepareProgram(gl, program);
  if (!CheckGLError(gl)) {
    filterProgram = nullptr;
    return false;
  }
  return true;
}

std::string LayerFilter::onBuildVertexShader() {
  return VERTEX_SHADER;
}

std::string LayerFilter::onBuildFragmentShader() {
  return FRAGMENT_SHADER;
}

void LayerFilter::onPrepareProgram(const tgfx::GLInterface*, unsigned) {
}

void LayerFilter::onUpdateParams(const tgfx::GLInterface*, const tgfx::Rect&, const tgfx::Point&) {
}

void LayerFilter::update(Frame frame, const tgfx::Rect& inputBounds, const tgfx::Rect& outputBounds,
                         const tgfx::Point& extraScale) {
  layerFrame = frame;
  contentBounds = inputBounds;
  transformedBounds = outputBounds;
  filterScale = extraScale;
}

static void EnableMultisample(const tgfx::GLInterface* gl, bool usesMSAA) {
  if (usesMSAA && gl->caps->multisampleDisableSupport) {
    gl->functions->enable(GL_MULTISAMPLE);
  }
}

static void DisableMultisample(const tgfx::GLInterface* gl, bool usesMSAA) {
  if (usesMSAA && gl->caps->multisampleDisableSupport) {
    gl->functions->disable(GL_MULTISAMPLE);
  }
}

void LayerFilter::draw(tgfx::Context* context, const FilterSource* source,
                       const FilterTarget* target) {
  if (source == nullptr || target == nullptr || !filterProgram) {
    LOGE(
        "LayerFilter::draw() can not draw filter, "
        "because the argument(source/target) is null");
    return;
  }
  auto gl = tgfx::GLInterface::Get(context);
  EnableMultisample(gl, needsMSAA());
  gl->functions->useProgram(filterProgram->program);
  gl->functions->disable(GL_SCISSOR_TEST);
  gl->functions->enable(GL_BLEND);
  gl->functions->blendEquation(GL_FUNC_ADD);
  gl->functions->blendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  gl->functions->bindFramebuffer(GL_FRAMEBUFFER, target->frameBufferID);
  gl->functions->viewport(0, 0, target->width, target->height);

  ActiveGLTexture(gl, GL_TEXTURE0, GL_TEXTURE_2D, source->textureID);
  gl->functions->uniformMatrix3fv(vertexMatrixHandle, 1, GL_FALSE, target->vertexMatrix.data());
  gl->functions->uniformMatrix3fv(textureMatrixHandle, 1, GL_FALSE, source->textureMatrix.data());
  onUpdateParams(gl, contentBounds, filterScale);
  auto vertices = computeVertices(contentBounds, transformedBounds, filterScale);
  bindVertices(gl, source, target, vertices);
  gl->functions->drawArrays(GL_TRIANGLE_STRIP, 0, 4);
  if (filterProgram->vertexArray > 0) {
    gl->functions->bindVertexArray(0);
  }
  DisableMultisample(gl, needsMSAA());
  CheckGLError(gl);
}

std::vector<tgfx::Point> LayerFilter::computeVertices(const tgfx::Rect& bounds,
                                                      const tgfx::Rect& transformed,
                                                      const tgfx::Point&) {
  std::vector<tgfx::Point> vertices = {};
  tgfx::Point contentPoint[4] = {{transformed.left, transformed.bottom},
                                 {transformed.right, transformed.bottom},
                                 {transformed.left, transformed.top},
                                 {transformed.right, transformed.top}};
  tgfx::Point texturePoints[4] = {{0.0f, bounds.height()},
                                  {bounds.width(), bounds.height()},
                                  {0.0f, 0.0f},
                                  {bounds.width(), 0.0f}};
  for (int ii = 0; ii < 4; ii++) {
    vertices.push_back(contentPoint[ii]);
    vertices.push_back(texturePoints[ii]);
  }
  return vertices;
}

void LayerFilter::bindVertices(const tgfx::GLInterface* gl, const FilterSource* source,
                               const FilterTarget* target, const std::vector<tgfx::Point>& points) {
  std::vector<float> vertices = {};
  for (size_t i = 0; i < points.size();) {
    auto vertexPoint = ToGLVertexPoint(target, source, contentBounds, points[i++]);
    vertices.push_back(vertexPoint.x);
    vertices.push_back(vertexPoint.y);
    auto texturePoint = ToGLTexturePoint(source, points[i++]);
    vertices.push_back(texturePoint.x);
    vertices.push_back(texturePoint.y);
  }

  if (filterProgram->vertexArray > 0) {
    gl->functions->bindVertexArray(filterProgram->vertexArray);
  }
  gl->functions->bindBuffer(GL_ARRAY_BUFFER, filterProgram->vertexBuffer);
  gl->functions->bufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), &vertices[0],
                            GL_STREAM_DRAW);
  gl->functions->vertexAttribPointer(static_cast<unsigned>(positionHandle), 2, GL_FLOAT, GL_FALSE,
                                     4 * sizeof(float), static_cast<void*>(0));
  gl->functions->enableVertexAttribArray(static_cast<unsigned>(positionHandle));

  gl->functions->vertexAttribPointer(textureCoordHandle, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                                     reinterpret_cast<void*>(2 * sizeof(float)));
  gl->functions->enableVertexAttribArray(textureCoordHandle);
  gl->functions->bindBuffer(GL_ARRAY_BUFFER, 0);
}

bool LayerFilter::needsMSAA() const {
  return Filter::needsMSAA();
}
}  // namespace pag
