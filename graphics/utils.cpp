#include "utils.hpp"

void diffassert_raw(const std::string_view& statement, const std::string_view& msg, const std::source_location& sl)
{
	std::cerr << sl.file_name() <<  ":" << sl.line() << ": " << sl.function_name() << ": " << std::endl
			  << "\tAssertion `" << statement << "` failed" << std::endl;
	if (msg.length() > 0)
		std::cerr << "\tMessage: " << msg << std::endl;
	throw false;
}
