#include "pch.h"

#include "VulkanEngine.h"


#ifdef _DEVELOP
int32_t main(int32_t argc, char* argv[])
#else
int __stdcall WinMain(void*, void*, char* cmdLine, int)
#endif
{
	TracySetProgramName("Hawsoo_Solanine_x64");

	{
		ZoneScopedN("Logo text");

		const char* logoText =
			"                .^~7?7^                                             !P5PPY7^                       \n"
			"                .?P#@@@BY!:                                          ~Y#@@@&P:                     \n"
			"                    G@@@@@5                                            !@@@@@~                     \n"
			"                    !@@@&!..........:!7!^                              J@@@@7                      \n"
			"   !^      ..:^~!7?JP@@@&GBBBBBBBBBB#@@@@GJ^                           G@@@P                       \n"
			"   ~&BPPGBB#&&&#BGP5YJ?7!~^:::::::::::Y@@@@@G^                        .#@@@~                       \n"
			"   :&@B^~!7!~^:.      ~PBBG5!       .Y@@@@&BB7                        ^@@@G                        \n"
			"   5@@@:    JGBB57     :#@@@&:     !5P5Y?~:                           J@@@7                        \n"
			"  Y@@@P     :B@@@&.     B@@@Y      .                                 .#@@&:                        \n"
			".P@@@&^     7@@@@?     .#@@#.                                        !@@@Y                         \n"
			"^@@@@!     ?@@@@?       5@@B~:.    .~!~:.                            B@@@7                         \n"
			" !5Y~    :P@@@#~         ?G&@@&#BBB#@@@@&G^                         J@@@@&Y:                       \n"
			"       .J&@@&5:            .^!?JY5PPPP5YJ7.                        7@@@&P&@&?                      \n"
			"     :Y&@&P7.           .:^~75P5YJ!                               !@@@&! :P@@B~                    \n"
			"   ^YB#5!:~~^^^~!7?JY5PB#@@@&&&&&#B^                             !@@@B:    7#@@5:                  \n"
			"   ...    :7P#&@@@&##@@@G7^:.....                               7@@@P.      .5@@&5^                \n"
			"             .:^^:. :&@@@!                                     Y@@@J          7@@@@P~              \n"
			"                    :&@@G                                    ^B@@B^            ^P@@@@G7.           \n"
			"                    :&@@J                                   J@@&J                !B@@@@#Y^         \n"
			"               ..:::7@@@P?JJJJJJJ?77?5GPJ!:               !B@@P^                   7B@@@@@B?^      \n"
			"  ~!7777?JYPGGB#&&&&&##BBBBBBBBB#&@@@@@@@@&5.           ~P@#Y~                       !B@@@@@@B?^   \n"
			"  :!YB@@@@@&#GYJ7!~^:.           .:^!J5G#&@#:         ^YGY~.                           !G&@@@@@@BJ:\n"
			"      :!7?^.                            .:^.         .^:                                 ^?YYYY5PP7\n"
			"                                                                                                   \n"
			"===================================================================================================\n"
			"                                                                                                   \n";
		std::cout << logoText << std::endl;
	}

	// @TODO: disable Sticky Keys right here!!! And then restore the setting to what it was before at the end.

	VulkanEngine engine;
	engine.init();
	engine.run();
	engine.cleanup();

	return 0;
}
