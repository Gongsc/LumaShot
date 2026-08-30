#pragma once
#include "Types.h"
#include <optional>

namespace lumashot {

class AnnotationDocument {
public:
    static constexpr std::size_t MaxHistory = 100;
    void add(Annotation annotation);
    bool replace(std::size_t index, Annotation annotation);
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    bool undo();
    bool redo();
    void clear() noexcept;
    [[nodiscard]] const std::vector<Annotation>& items() const noexcept { return items_; }

private:
    enum class ChangeKind { Add, Replace };
    struct Change {
        ChangeKind kind{};
        std::size_t index{};
        std::optional<Annotation> before;
        std::optional<Annotation> after;
        std::optional<Annotation> evicted;
    };

    std::vector<Annotation> items_;
    std::vector<Change> undo_;
    std::vector<Change> redo_;

    void remember(Change change);
};

} // namespace lumashot
