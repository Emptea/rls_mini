#include "rls_mini_version.h"
#include "shared_data.h"

#include <picli.h>
#include <pikbdlistener.h>
#include <piliterals_time.h>
#include <piscreen.h>
#include <pisignals.h>

using namespace PICoutManipulators;

void printVersion() {
	piCout << Cyan << "Versions:";
	piCout << "  RLS Mini:" << Bold << RLS_MINI_VERSION_NAME;
	piCout << "       PIP:" << Bold << PIP_VERSION_NAME;
}

void help() {
	piCout << Bold << "RLS Mini";
	printVersion();
	piCout << "";
	piCout << Green << Bold << "Usage:" << Default << "\"rls_mini [-vhd]\"" << NewLine;
	piCout << Green << Bold << "Details:";
	piCout << "-v --version " << Green << "- display versions and exit";
	piCout << "-h --help    " << Green << "- display this message and exit";
	piCout << "-d --debug   " << Green << "- enable debug output";
	// piCout << "-c --console " << Green << "- start with console UI";
}


int main(int argc, char * argv[]) {
	PICLI cli(argc, argv);
	cli.addArgument("version");
	cli.addArgument("help");
	cli.addArgument("debug");
	if (cli.hasArgument("version")) {
		printVersion();
		return 0;
	}
	if (cli.hasArgument("help")) {
		help();
		return 0;
	}
	piDebug = cli.hasArgument("debug");

	PISignals::setSlot([](PISignals::Signal s) {
		piCout << "Signal" << s;
		PIKbdListener::exiting = true;
		PISignals::releaseSignals(s);
	});
	PISignals::grabSignals(PISignals::Interrupt | PISignals::Termination);

	PIKbdListener * kbd = nullptr;

	kbd                 = new PIKbdListener(nullptr, nullptr, false);
	kbd->enableExitCapture(PIKbdListener::F10);

	GLOBAL->init();
	piCout << "init done";

	GLOBAL->start();
	piCout << "started";

	/*
	250_ms .sleep();
	Protocol_RLS_Mini::POI_TK_Kvit msg;
	msg.words << 1 << 2 << 3;
	GLOBAL->sendMessage(msg);
	*/

	kbd->start();
	WAIT_FOR_EXIT;

	GLOBAL->stop();
	piCout << "stop done";

	piDeleteSafety(kbd);

	return 0;
}
