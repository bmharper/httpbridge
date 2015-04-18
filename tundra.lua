Build {
	Units = function ()
		local example_backend = Program {
			Name = "example-backend",
			Sources = {
				"cpp/example-backend.cpp",
				"cpp/http-bridge.cpp",
			},
			Includes = {
				"cpp/flatbuffers/include",
			},
			Libs = {
				{ "Ws2_32.lib"; Config = "win*" },
				{ "stdc++"; Config = {"*-gcc-*", "*-clang-*"} },
			},
		}

		local server = Program {
			Name = "server",
			Sources = {
				"cpp/server/server.cpp",
				"cpp/server/server.h",
				"cpp/server/main.cpp",
				"cpp/server/http11/http11_common.h",
				"cpp/server/http11/http11_parser.c",
				"cpp/server/http11/http11_parser.h",
				"cpp/http-bridge.cpp",
			},
			Includes = {
				"cpp/flatbuffers/include",
			},
			Libs = {
				{ "Ws2_32.lib"; Config = "win*" },
				{ "stdc++"; Config = {"*-gcc-*", "*-clang-*"} },
			},
		}

		Default(example_backend, server)
	end,

	Env = {
		CXXOPTS = {
			{ "/W3"; Config = "win*" },
			{ "/EHsc"; Config = "win*" },
			{ "/O2"; Config = "*-msvc-release" },
			{ "/analyze"; Config = "*-msvc-release" },
			{ "-std=c++11"; Config = {"*-gcc-*", "*-clang-*"} },
		},
		GENERATE_PDB = {
			{ "0"; Config = "*-msvc-release" },
			{ "1"; Config = { "*-msvc-debug", "*-msvc-production" } },
		},
	},

	Configs = {
		Config {
			Name = "macosx-gcc",
			DefaultOnHost = "macosx",
			Tools = { "gcc" },
		},
		Config {
			Name = "linux-gcc",
			DefaultOnHost = "linux",
			Tools = { "gcc" },
		},
		Config {
			Name = "freebsd-clang",
			DefaultOnHost = "freebsd",
			Tools = { "clang" },
		},
		Config {
			Name = "win32-msvc",
			SupportedHosts = { "windows" },
			Tools = { { "msvc-vs2013"; TargetArch = "x86" } },
		},
		Config {
			Name = "win64-msvc",
			DefaultOnHost = "windows",
			Tools = { { "msvc-vs2013"; TargetArch = "x64" } },
		},
	},

	IdeGenerationHints = {
		Msvc = {
			-- Remap config names to MSVC platform names (affects things like header scanning & debugging)
			PlatformMappings = {
				['win64-msvc'] = 'x64',
				['win32-msvc'] = 'Win32',
			},
			-- Remap variant names to MSVC friendly names
			VariantMappings = {
				['release']    = 'Release',
				['debug']      = 'Debug',
				['production'] = 'Production',
			},
		},
		-- Override solutions to generate and what units to put where.
		MsvcSolutions = {
			['httpbridge.sln'] = {},          -- receives all the units due to empty set
		},

		-- Override output directory for sln/vcxproj files.
		MsvcSolutionDir = 'vs2013',

	    BuildAllByDefault = true,
	}
}