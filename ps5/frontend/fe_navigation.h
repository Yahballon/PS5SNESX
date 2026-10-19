// PS5NESX circular CoverFlow selection, separate from GPU presentation.
#pragma once
namespace fe {
inline int WrappedGameIndex(int selected, int delta, int count)
{
    if (count <= 0) return 0;
    const long long candidate = static_cast<long long>(selected) + delta;
    return static_cast<int>((candidate % count + count) % count);
}
} // namespace fe
