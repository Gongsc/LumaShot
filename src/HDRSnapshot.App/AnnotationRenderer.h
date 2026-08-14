#pragma once
#include "ColorPipeline.h"
#include <HDRSnapshot/AnnotationDocument.h>

namespace hdrsnapshot {
class AnnotationRenderer {
public:
    static void renderSdr(ImageBgra8& image, const AnnotationDocument& annotations);
    static void renderHdr(ImageF16& image, const AnnotationDocument& annotations);
};
} // namespace hdrsnapshot

