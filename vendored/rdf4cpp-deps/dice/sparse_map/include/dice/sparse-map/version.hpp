#ifndef DICE_SPARSEMAP_VERSION_HPP
#define DICE_SPARSEMAP_VERSION_HPP

#include <array>

namespace dice::sparse_map {
	inline constexpr char name[] = "dice-sparse-map";
	inline constexpr char version[] = "0.2.9";
	inline constexpr std::array<int, 3> version_tuple = {0, 2, 9};
	inline constexpr int pobr_version = 1; ///< persisted object binary representation version
} // namespace dice::sparse_map

#endif // DICE_SPARSEMAP_VERSION_HPP
