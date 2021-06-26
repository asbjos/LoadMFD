#pragma once

/*
Header file for LoadMFD2 addon for Orbiter Space Flight Simulator 2016.
Addon by Asbjørn 'asbjos' Krüger, 2020.

This source code is released under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
For other use, please contact me (I'm username 'asbjos' on Orbiter-Forum).
*/

enum LOADUNIT { UNITMSS, UNITG, UNITLOCALG, LOADLASTENTRY };

static char MFD_NAME[10] = "LoadA"; // debug, change at end
const int LOAD_LOG_SIZE = 480;
const double LOAD_LOG_TIME_STEP = 0.25; // sample every 0.25 seconds, giving with log size a total of 120 seconds of history.
const double LOAD_LOG_TOTAL_TIME_RANGE = double(LOAD_LOG_SIZE) * LOAD_LOG_TIME_STEP;

struct {
	int LOAD_UNIT = 0;
	bool SHOW_TEXT = false;
	bool SHOW_COMPONENTS = false;
} DEFAULT_VALUE;

typedef struct LoadDataType {
	double load;
	VECTOR3 loadComp;
	float time;
} LOG;


class LoadMFD : public MFD2
{
public:
	LoadMFD(DWORD w, DWORD h, VESSEL* vessel);
	~LoadMFD();
	char* ButtonLabel(int bt);
	int ButtonMenu(const MFDBUTTONMENU** menu) const;
	bool Update(oapi::Sketchpad* skp);
	static int MsgProc(UINT msg, UINT mfd, WPARAM wparam, LPARAM lparam);
	bool ConsumeButton(int bt, int event);
	bool ConsumeKeyBuffered(DWORD key);
	//void StoreStatus() const;
	void RecallStatus();
	//void myFunction(void);
	//void clbkPreStep(double simt, double simdt, double mjd);
	//void oapi::Module::clbkPreStep(double simt, double simdt, double mjd);

	//char* GetLoadUnitName(LOADUNIT Unit);
	//double GetLoadUnitFactor(LOADUNIT Unit, OBJHANDLE ref);
	void PlotLine(double x0, double y0, double x1, double y1, oapi::Sketchpad* skp);
private:
	//bool LoadAuto;

	int W, H;
	//OBJHANDLE VesselHandle;
	//double VesselMass;
	//VECTOR3 VesselForce, VesselWeight;
	VESSEL* ves;
};

// My variables
LOG loadHistory[LOAD_LOG_SIZE];
int loadHistoryIndex = 0;
int loadHistoryLength = 0;
double loadHistoryStartTime = 0.0;

LOADUNIT loadUnit = LOADUNIT(DEFAULT_VALUE.LOAD_UNIT);

bool showText = DEFAULT_VALUE.SHOW_TEXT;
bool showComponents = DEFAULT_VALUE.SHOW_COMPONENTS;

double globalMaxLoad = 0.0;

// ==============================================================
// Global variables

int g_MFDmode; // identifier for new MFD mode
//bool ready = false;
HINSTANCE hInst;

//double VesselAcceleration, AccX, AccY, AccZ;
NOTEHANDLE LoadText, CompText;
//bool WriteText = false;
//bool ShowComponents = false;
//double LoadArray[239];
//double LargestLoad;
//int n = 0;
//double OldSimTime;

//static struct {
//	double nextTime;
//	int nextSample;
//	float* time;
//	float* load;
//	float* loadG;
//} LoadData;

//int Action = 0;
//char LoadUnit[] = "m/s²";
//int LUnit = 0;
//double GFactor = 1;
//double AbsolutelyLargest;
//double diff;
//double TimeReset = 0;

//static bool SaveWriteText;

static bool resetCommand = true;

// Static memory for Store/Recall Status
//struct {
//
//	LOG loadHistory[LOAD_LOG_SIZE];
//	int loadHistoryIndex;
//	int loadHistoryLength;
//
//	LOADUNIT loadUnit;
//
//	bool showText;
//	bool showComponents;
//
//	double globalMaxLoad;
//} Memory;

// ==============================================================
// API interface

DLLCLBK void InitModule(HINSTANCE hDLL)
{
	MFDMODESPECEX spec;
	spec.name = MFD_NAME;
	spec.key = OAPI_KEY_L;                // MFD mode selection key
	spec.context = NULL;
	spec.msgproc = LoadMFD::MsgProc;  // MFD mode callback function

	/*LoadData.nextTime = 0.0;
	LoadData.nextSample = 0;
	LoadData.time = new float[AmmountOfPoints]; memset(LoadData.time, 0, AmmountOfPoints * sizeof(float));
	LoadData.loadG = new float[AmmountOfPoints]; memset(LoadData.loadG, 0, AmmountOfPoints * sizeof(float));
	LoadData.load = new float[AmmountOfPoints]; memset(LoadData.load, 0, AmmountOfPoints * sizeof(float));*/


	// Register the new MFD mode with Orbiter
	g_MFDmode = oapiRegisterMFDMode(spec);
}

DLLCLBK void ExitModule(HINSTANCE hDLL)
{
	// Unregister the custom MFD mode when the module is unloaded
	oapiUnregisterMFDMode(g_MFDmode);

	oapiDelAnnotation(LoadText);
	oapiDelAnnotation(CompText);

	resetCommand = true; // tell MFD that we have locked Orbiter, and don't want to recall from RecallStatus.
	/*delete[]LoadData.load;
	delete[]LoadData.time;
	delete[]LoadData.loadG;*/
}

// ==============================================================
// MFD class implementation

// Constructor
LoadMFD::LoadMFD(DWORD w, DWORD h, VESSEL* vessel)
	: MFD2(w, h, vessel)
{
	ves = vessel;
	//Action = 0;

	W = w;
	H = h;

	//int g;

	//g = AddGraph();
	//SetAxisTitle(g, 0, "Time: s");
	//SetAxisTitle(g, 1, "Load: m/s²");
	//AddPlot(g, LoadData.time, LoadData.load, AmmountOfPoints, 1, &LoadData.nextSample);

	//g = AddGraph();
	//SetAxisTitle(g, 0, "Time: s");
	//SetAxisTitle(g, 1, "Load: G");
	//AddPlot(g, LoadData.time, LoadData.loadG, AmmountOfPoints, 1, &LoadData.nextSample);


	//LoadAuto = true;
	//LUnit = 0;
}

LoadMFD::~LoadMFD()
{
	oapiDelAnnotation(LoadText);
	oapiDelAnnotation(CompText);
}

const int TOTAL_BUTTONS = 12;

// Return button labels
char* LoadMFD::ButtonLabel(int bt)
{
	// The labels for the two buttons used by our MFD mode
	static char* label[TOTAL_BUTTONS] = { "LOAD", "RST", "UNIT", "COMP", " ", " ", " ", " ", " ", " ", " ", "NULL" };
	return (bt < TOTAL_BUTTONS ? label[bt] : 0);
}

// Return button menus
int LoadMFD::ButtonMenu(const MFDBUTTONMENU** menu) const
{
	// The menu descriptions for the two buttons
	static const MFDBUTTONMENU mnu[TOTAL_BUTTONS] = {
		{ "Write load on screen", 0, 'L' },
		{ "Reset load", 0, 'R' },
		{ "Set load unit", 0, 'U' },
		{ "Show components", 0, 'C' },
		{ " ", 0, ' ' },
		{ " ", 0, ' ' },
		{ " ", 0, ' ' },
		{ " ", 0, ' ' },
		{ " ", 0, ' ' },
		{ " ", 0, ' ' },
		{ " ", 0, ' ' },
		{ "Reset all", 0, 'N' },
	};
	if (menu) *menu = mnu;
	return TOTAL_BUTTONS; // return the number of buttons used
}

char* GetLoadUnitName(LOADUNIT Unit)
{
	switch (Unit)
	{
	case UNITMSS:
		return "m/s²";
	case UNITG:
		return "G";
	case UNITLOCALG:
		return "g";
	case LOADLASTENTRY:
	default:
		break;
	}

	return "ERROR";
}

double GetLoadUnitFactor(LOADUNIT Unit, OBJHANDLE ref)
{
	switch (Unit)
	{
	case UNITMSS:
		return 1.0;
	case UNITG:
		return G;
	case UNITLOCALG:
		return GGRAV * oapiGetMass(ref) / oapiGetSize(ref) / oapiGetSize(ref);
	case LOADLASTENTRY:
	default:
		break;
	}

	return 1.0;
}