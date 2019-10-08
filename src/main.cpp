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
	std::string vcvars_script;
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
		std::wstring key ( env_string.begin(), key_end );
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

void run_script( std::string path )
{
	#ifdef WIN32
		std::wstring wpath = L"cmd.exe /C " + std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(path);
		PROCESS_INFORMATION proc_info{};
		//todo: close process handle?
		STARTUPINFO startup_info{};
		if( CreateProcess( NULL, &wpath[0], nullptr, nullptr, false, CREATE_UNICODE_ENVIRONMENT, NULL, NULL, &startup_info, &proc_info) )
		{
			std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
			TerminateProcess( proc_info.hProcess, 0);
		}
		else
		{
			auto error = GetLastError();

			throw std::runtime_error( "could not launch script " + std::to_string(error) );
		}
	#endif
}

bool parse_config( int argc, char** argv, config& conf )
{
	if( argc < 1 )
	{
		return false;
	}
	conf.vcvars_script = argv[0];
	conf.storage_path = "vcvars-pre.tmp"; // default
	return true;
}

void write_bash_set( const std::wstring &key, const std::wstring &value, std::wostream &out )
{
	out << L"export ";
	out << key;
	out << L"=\"" << value;
	if( !value.empty() && *std::prev( value.end() ) == L'\\' )
	{
		out << L'\\';
	}
	out << L'"' << std::endl;
}

void write_bash_add( const std::wstring &key, const std::wstring &old_value, const std::wstring &new_value, std::wostream &out )
{
	out << L"export ";
	out << key;
	//todo: split new_value/old_value by ; and only add new ones, in separate lines
	out << L"=\"$" << key << L':' << new_value;
	if( !new_value.empty() && *std::prev( new_value.end() ) == L'\\' )
	{
		out << L'\\';
	}
	out << L'"' << std::endl;
}

void generate_bash_script( const environment_strings &pre, const environment_strings &post, std::wostream &out )
{
	for( auto [ key, value ] : post )
	{
		auto pre_it = pre.find( key );
		if( pre_it == pre.end() )
		{
			write_bash_set( key, value, out );
		}
		else if( pre_it->second != value )
		{
			write_bash_add( key, pre_it->second, value, out );
		}
	}
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

		generate_bash_script( pre_script, post_script, out );
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