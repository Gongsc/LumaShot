#include <LumaShot/AnnotationDocument.h>

namespace lumashot {

void AnnotationDocument::remember(Change change) {
    if (undo_.size() == MaxHistory) undo_.erase(undo_.begin());
    undo_.push_back(std::move(change));
    redo_.clear();
}

void AnnotationDocument::add(Annotation annotation) {
    Change change{ChangeKind::Add, items_.size(), std::nullopt, annotation, std::nullopt};
    if (items_.size() == MaxHistory) {
        change.evicted = std::move(items_.front());
        items_.erase(items_.begin());
        change.index = items_.size();
    }
    items_.push_back(std::move(annotation));
    remember(std::move(change));
}

bool AnnotationDocument::replace(std::size_t index, Annotation annotation) {
    if (index >= items_.size()) return false;
    Change change{ChangeKind::Replace, index, items_[index], annotation, std::nullopt};
    items_[index] = std::move(annotation);
    remember(std::move(change));
    return true;
}

bool AnnotationDocument::canUndo() const noexcept { return !undo_.empty(); }
bool AnnotationDocument::canRedo() const noexcept { return !redo_.empty(); }

bool AnnotationDocument::undo() {
    if (undo_.empty()) return false;
    const Change& change = undo_.back();
    if (change.kind == ChangeKind::Add) {
        if (items_.empty()) return false;
        items_.pop_back();
        if (change.evicted) items_.insert(items_.begin(), *change.evicted);
    } else {
        if (change.index >= items_.size() || !change.before) return false;
        items_[change.index] = *change.before;
    }
    redo_.push_back(std::move(undo_.back()));
    undo_.pop_back();
    return true;
}

bool AnnotationDocument::redo() {
    if (redo_.empty()) return false;
    const Change& change = redo_.back();
    if (change.kind == ChangeKind::Add) {
        if (!change.after) return false;
        if (change.evicted && !items_.empty()) items_.erase(items_.begin());
        items_.push_back(*change.after);
    } else {
        if (change.index >= items_.size() || !change.after) return false;
        items_[change.index] = *change.after;
    }
    undo_.push_back(std::move(redo_.back()));
    redo_.pop_back();
    return true;
}

void AnnotationDocument::clear() noexcept {
    items_.clear();
    undo_.clear();
    redo_.clear();
}

} // namespace lumashot
