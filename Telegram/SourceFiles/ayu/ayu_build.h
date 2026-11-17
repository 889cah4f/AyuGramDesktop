// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ui/text/text_entity.h"

namespace AyuBuild {

[[nodiscard]] bool IsCommunity();
[[nodiscard]] QString Tag();
[[nodiscard]] QString Author();
[[nodiscard]] QString AuthorLink();
[[nodiscard]] QString VersionSuffix();
[[nodiscard]] TextWithEntities ByAuthor();

} // namespace AyuBuild
