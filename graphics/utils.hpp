#pragma once

#include <source_location>
#include <iostream>
#include <string_view>

static const std::string_view _empty_sv("");

#define diffassert_msg(statement, msg) \
	if (!(statement)) { \
		auto sl = std::source_location::current(); \
		diffassert_raw(#statement, msg, sl); \
	}
		
#define diffassert(statement) diffassert_msg(statement, _empty_sv)

void diffassert_raw(const std::string_view& statement, const std::string_view& msg, const std::source_location& sl);
