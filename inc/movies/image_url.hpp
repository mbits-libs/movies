// Copyright (c) 2023 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <movies/movie_info.hpp>

namespace movies::v1 {
	template <typename Cb>
	void visit_image(image_url& image, Cb const& cb) {
		cb(image);
	}

	template <typename Cb>
	void visit_image(std::optional<image_url>& image, Cb const& cb) {
		if (image) cb(*image);
	}

	template <typename Cb>
	void visit_image(std::vector<image_url>& dst, Cb const& cb) {
		for (auto& image : dst) {
			movies::visit_image(image, cb);
		}
	}

	template <typename Cb>
	void visit_image(poster_info& dst, Cb const& cb) {
		movies::visit_image(dst.small, cb);
		movies::visit_image(dst.normal, cb);
		movies::visit_image(dst.large, cb);
	}

	template <typename Payload, typename Cb>
	void visit_image(translatable<Payload>& dst, Cb const& cb) {
		for (auto& [_, dst] : dst.items) {
			movies::visit_image(dst, cb);
		}
	}

	template <typename Cb>
	void visit_image(image_info& self, Cb const& cb) {
		movies::visit_image(self.highlight, cb);
		movies::visit_image(self.poster, cb);
		movies::visit_image(self.gallery, cb);
	}
}  // namespace movies::v1
