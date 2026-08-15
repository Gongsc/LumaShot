#include <LumaShot/AnnotationDocument.h>

namespace lumashot {

void AnnotationDocument::add(Annotation annotation) {
    if (items_.size() == MaxHistory) items_.erase(items_.begin());
    items_.push_back(std::move(annotation));
    redo_.clear();
}

bool AnnotationDocument::canUndo() const noexcept { return !items_.empty(); }
bool AnnotationDocument::canRedo() const noexcept { return !redo_.empty(); }

bool AnnotationDocument::undo() {
    if (items_.empty()) return false;
    redo_.push_back(std::move(items_.back()));
    items_.pop_back();
    return true;
}

bool AnnotationDocument::redo() {
    if (redo_.empty()) return false;
    items_.push_back(std::move(redo_.back()));
    redo_.pop_back();
    return true;
}

void AnnotationDocument::clear() noexcept {
    items_.clear();
    redo_.clear();
}

} // namespace lumashot

