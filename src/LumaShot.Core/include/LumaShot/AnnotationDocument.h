#pragma once
#include "Types.h"
#include <optional>

namespace lumashot {

class AnnotationDocument {
public:
    static constexpr std::size_t MaxHistory = 100;
    void add(Annotation annotation);
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    bool undo();
    bool redo();
    void clear() noexcept;
    [[nodiscard]] const std::vector<Annotation>& items() const noexcept { return items_; }

private:
    std::vector<Annotation> items_;
    std::vector<Annotation> redo_;
};

} // namespace lumashot

