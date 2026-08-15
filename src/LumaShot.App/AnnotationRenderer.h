#pragma once
#include "ColorPipeline.h"
#include <LumaShot/AnnotationDocument.h>

namespace lumashot {
class AnnotationRenderer {
public:
    static void renderSdr(ImageBgra8& image, const AnnotationDocument& annotations);
    static void renderHdr(ImageF16& image, const AnnotationDocument& annotations);
};
} // namespace lumashot

