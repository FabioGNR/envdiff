#include <iostream>
#include <string>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#include <windows.h>
#include <processenv.h>
#include <map>
#include <fstream>
#include <memory>
#include <locale>
#include <codecvt>
#include <thread>

struct config {
	std::string storage_path;
	std::string identifier;
};

class environment_strings_handle {
	using T = decltype( GetEnvironmentStrings() );

	public:
		environment_strings_handle( T value ) : value_( value )
		{
		}

		T& operator*()
		{
			return value_;
		}

		~environment_strings_handle()
		{
			FreeEnvironmentStrings( value_ );
		}
	private:
		T value_;
};

using environment_strings = std::map< std::wstring, std::wstring >;

std::wstring convert_key_to_unix( const std::wstring &key )
{
	static std::map< std::wstring, std::wstring > map {
		{ L"Path", L"PATH" }
	};
	auto map_it = map.find( key );
	if( map_it != map.end() )
	{
		return map_it->second;
	}
	return key;
}

environment_strings get_environment_strings()
{
	environment_strings_handle strings( GetEnvironmentStrings() );
	environment_strings vars;
	auto* str_ptr = *strings;
	while( *str_ptr )
	{
		std::wstring env_string ( str_ptr );
		str_ptr += env_string.size() + 1;
		auto key_end = std::find( env_string.begin(), env_string.end(), L'=' );
		std::wstring key = convert_key_to_unix( std::wstring( env_string.begin(), key_end ) );
		std::wstring value ( std::next( key_end ), env_string.end() );
		vars.insert( { key, value } );
	}
	return vars;
}

void store( std::wofstream &storage, const environment_strings &vars )
{
	for( const auto& [ key, value ] : vars)
	{
		storage << key << std::endl;
		storage << value << std::endl;
	}
}

environment_strings from_storage( std::wifstream &storage )
{
	environment_strings vars;
	while( storage )
	{
		std::wstring key;
		std::wstring value;
		if( std::getline( storage, key ) && std::getline( storage, value ) )
		{
			vars.insert( { key, value } );
		}
	}
	return vars;
}

bool parse_config( int argc, char** argv, config& conf )
{
	conf.identifier = argc >= 2 ? argv[1] : "vcvars";
	conf.storage_path = argc >= 3 ? argv[2] : "vcvars-pre.tmp";
	return true;
}

void write_bash_set( const std::wstring &prefix, const std::wstring &key, const std::wstring &value, std::wostream &out )
{
	out << prefix;
	out << L"export ";
	out << key;
	out << L"=\"" << value;
	if( !value.empty() && *std::prev( value.end() ) == L'\\' )
	{
		out << L'\\';
	}
	out << L'"' << std::endl;
}

std::vector< std::wstring > split_values( const std::wstring &value )
{
	std::vector< std::wstring > values;
	auto start_it = value.begin();
	auto separator_it = std::find( value.begin(), value.end(), L';' );
	while( separator_it != value.end() )
	{
		if( start_it != separator_it )
		{
			values.emplace_back( start_it, separator_it );
		}
		start_it = std::next( separator_it );
		if( start_it != value.end() )
		{
			separator_it = std::find( start_it, value.end(), L';' );
		}
		else
		{
			break;
		}
	}
	if( start_it != value.end() )
	{
		// add last value
		values.emplace_back( start_it, separator_it );
	}
	return values;
}

void write_bash_add( const std::wstring &prefix, const std::wstring &key, const std::wstring &new_value, std::wostream &out )
{
	out << prefix;
	out << L"export ";
	out << key;
	out << L"=\"$" << key << L':' << new_value;
	if( !new_value.empty() && *std::prev( new_value.end() ) == L'\\' )
	{
		out << L'\\';
	}
	out << L'"' << std::endl;
}

void write_bash_add( const std::wstring &prefix, const std::wstring &key, const std::wstring &old_value, const std::wstring &new_value, std::wostream &out )
{
	auto old_values = split_values( old_value );
	auto new_values = split_values( new_value );
	for( const auto &value : new_values )
	{
		auto old_it = std::find( old_values.begin(), old_values.end(), value );
		if( old_it == old_values.end() )
		{
			write_bash_add( prefix, key, value, out );
		}
	}
}

const static auto pre_prefix = L"__PRE_VCVARS_";

void generate_bash_script( const std::string &identifier, const environment_strings &pre, const environment_strings &post, std::wostream &out )
{
	const std::wstring widentifier( identifier.begin(), identifier.end() );
	// write set function
	out << std::endl << L"function set_" + widentifier + L"() {" << std::endl;
	for( auto [ key, value ] : post )
	{
		auto pre_it = pre.find( key );
		if( pre_it == pre.end() )
		{
			std::wcout << L"setting " << key << std::endl;
			write_bash_set( L"\t", key, value, out );
		}
		else if( pre_it->second != value )
		{
			const auto pre_key = pre_prefix + key;
			const auto pre_value = L"${" + key + L"}";
			write_bash_set( L"\t", pre_key, pre_value, out );
			std::wcout << L"adding to " << key << std::endl;
			write_bash_add( L"\t", key, pre_it->second, value, out );
		}
	}
	out << L"}" << std::endl;
	// write reset function
	out << std::endl << L"function reset_" + widentifier + L"() {" << std::endl;
	for( auto [ key, value ] : post )
	{
		auto pre_it = pre.find( key );
		if( pre_it == pre.end() )
		{
			// clear variable
			write_bash_set( L"\t", key, L"", out );
		}
		else if( pre_it->second != value )
		{
			// reset to pre-value
			const auto pre_key = pre_prefix + key;
			write_bash_set( L"\t", key, L"${" + pre_key + L"}", out );
		}
	}
	out << L"}" << std::endl;
}

int main( int argc, char** argv )
{
	config conf {};
	if( !parse_config( argc, argv, conf ) )
	{
		return -1;
	}
	std::wifstream pre_storage ( conf.storage_path );
	if( pre_storage )
	{
		auto pre_script = from_storage( pre_storage );
		auto post_script = get_environment_strings();
		std::wofstream out("vcvars.sh");

		generate_bash_script( conf.identifier, pre_script, post_script, out );
		std::cout << "Stored vcvars bash in vcvars.sh";
		return 0;
	}
	else
	{
		auto pre_script = get_environment_strings();
		store( std::wofstream( conf.storage_path ), pre_script );
		std::cout << "Stored environment variables, run again after calling vcvars";
		return 0;
	}
}