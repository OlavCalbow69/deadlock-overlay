#pragma once

#include "core/product_info.h"

namespace solace::brand
{
struct credit_profile
{
    const char* user_name;
    const char* user_github;
    const char* user_initials;
    const char* author;
    const char* repo;
};

namespace profiles
{
constexpr credit_profile pondot{
    "Pondot",
    "github.com/poncippg-spec",
    "P",
    "Pondot",
    "github.com/poncippg-spec/Free-Solace-ImGui-Interface",
};

constexpr credit_profile placeholder{
    "Nova Dev",
    "github.com/dev-placeholder",
    "ND",
    "Nova Dev",
    "github.com/dev-placeholder/Free-Solace-ImGui-Interface",
};
} // namespace profiles

// Select the visible creator profile.
constexpr const credit_profile& active = profiles::pondot;

constexpr const char* product = solace::product_info::name;

constexpr const char* user_name = active.user_name;
constexpr const char* user_github = active.user_github;
constexpr const char* user_initials = active.user_initials;

constexpr const char* author = active.author;
constexpr const char* repo = active.repo;

constexpr const char* game = "Solace Alpha";
} // namespace solace::brand
