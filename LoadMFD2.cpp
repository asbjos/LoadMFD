// Shameless copy of Phoenix's Load MFD, which he/she apparently removed from OrbitHangar
// An MFD which shows the acceleration on the vessel from contact forces, with text on screen
// and a graph in a MFD

//Addon by Asbjørn 'asbjos' Krüger, 2016-2020.
//This source code is released under a Creative Commons Attribution - NonCommercial - ShareAlike 4.0 International License.
//For other use, please contact me (I'm username 'asbjos' on Orbiter-Forum).

#define STRICT
#define ORBITER_MODULE

#include "Orbitersdk.h"

enum LOADUNIT { UNITMSS, UNITG, UNITLOCALG, LOADLASTENTRY };

static char MFD_NAME[10] = "Load";
const int NUM_POINTS = 480;
const double SAMPLE_DT = 0.25; // sample every 0.25 seconds, giving with log size a total of 120 seconds of history.

struct {
	int LOAD_UNIT = 0;
	bool SHOW_TEXT = false;
	bool SHOW_COMPONENTS = false;
} DEFAULT_VALUE;

class LoadMFD : public GraphMFD
{
public:
	LoadMFD(DWORD w, DWORD h, VESSEL* vessel);
	~LoadMFD();
	char* ButtonLabel(int bt);
	int ButtonMenu(const MFDBUTTONMENU** menu) const;
	void Update(HDC hDC);
	static int MsgProc(UINT msg, UINT mfd, WPARAM wparam, LPARAM lparam);
	bool ConsumeButton(int bt, int event);
	bool ConsumeKeyBuffered(DWORD key);
	bool SetComponentsPlotRange(char* rstr);
};

// ==============================================================
// Global variables

int g_MFDmode; // identifier for new MFD mode

NOTEHANDLE LoadText, CompText;

double globalMaxLoad = 0.0;

static struct {
	int nextSample;
	double nextTime;
	float *time;
	float *load;
	float* loadToPlot; // load times some factor
	float *compX;
	float* compY;
	float* compZ;
	float* compXToPlot;
	float* compYToPlot;
	float* compZToPlot;
} loadData;

double VesselAcceleration;
char* LoadUnit;
double GFactor;
double timeReset = 0.0;
double previousSimt = -INFINITY;

LOADUNIT loadUnit = LOADUNIT(DEFAULT_VALUE.LOAD_UNIT);
bool showText = DEFAULT_VALUE.SHOW_TEXT;
bool showComponents = DEFAULT_VALUE.SHOW_COMPONENTS;

bool componentRangeAuto = true;
double componentRange = 1.0; // will be set by user anyway, so doesn't really need to be initialised.

bool parabolicModeActive = false; // set to true to activate experimental parabolic arc button
bool zeroGParabolicFlight = false;
double PIDintegral = 0.0;
double PIDpreviousError = 0.0;
double PIDintegralThrust = 0.0;
double PIDpreviousErrorThrust = 0.0;
double PIDintegralYaw = 0.0;
double PIDpreviousErrorYaw = 0.0;
bool pullUp = false;
int targetMaxEnergy = 0;
double timeWeightless = 0.0;
double thisTimeWeightless = 0.0;
NOTEHANDLE ParabolicText;

// ==============================================================
// API interface

void ClearLoadData(void)
{
	loadData.nextSample = 0;
	loadData.nextTime = 0.0;
	loadData.time = new float[NUM_POINTS]; memset(loadData.time, 0, NUM_POINTS * sizeof(float));
	loadData.load = new float[NUM_POINTS]; memset(loadData.load, 0, NUM_POINTS * sizeof(float));
	loadData.loadToPlot = new float[NUM_POINTS]; memset(loadData.loadToPlot, 0, NUM_POINTS * sizeof(float));
	loadData.compX = new float[NUM_POINTS]; memset(loadData.compX, 0, NUM_POINTS * sizeof(float));
	loadData.compY = new float[NUM_POINTS]; memset(loadData.compY, 0, NUM_POINTS * sizeof(float));
	loadData.compZ = new float[NUM_POINTS]; memset(loadData.compZ, 0, NUM_POINTS * sizeof(float));
	loadData.compXToPlot = new float[NUM_POINTS]; memset(loadData.compXToPlot, 0, NUM_POINTS * sizeof(float));
	loadData.compYToPlot = new float[NUM_POINTS]; memset(loadData.compYToPlot, 0, NUM_POINTS * sizeof(float));
	loadData.compZToPlot = new float[NUM_POINTS]; memset(loadData.compZToPlot, 0, NUM_POINTS * sizeof(float));

	globalMaxLoad = 0.0;
}

DLLCLBK void InitModule(HINSTANCE hDLL)
{
	MFDMODESPECEX spec;
	spec.name = MFD_NAME;
	spec.key = OAPI_KEY_L; // MFD mode selection key
	spec.context = NULL;
	spec.msgproc = LoadMFD::MsgProc; // MFD mode callback function

	ClearLoadData();

	// Register the new MFD mode with Orbiter
	g_MFDmode = oapiRegisterMFDMode(spec);

	// Get some defaults from the config file
	FILEHANDLE cfgFile = oapiOpenFile("MFD\\VesselLoadMFD.cfg", FILE_IN_ZEROONFAIL, CONFIG);
	if (cfgFile == 0)
	{
		oapiWriteLog("VesselLoadMFD failed to find config file!");
	}
	else
	{
		if (!oapiReadItem_int(cfgFile, "DefaultUnit", DEFAULT_VALUE.LOAD_UNIT)) oapiWriteLog("VesselLoadMFD could not read default unit!");
		if (!oapiReadItem_bool(cfgFile, "DefaultShowText", DEFAULT_VALUE.SHOW_TEXT)) oapiWriteLog("VesselLoadMFD could not read default showText!");
		if (!oapiReadItem_bool(cfgFile, "DefaultShowComponents", DEFAULT_VALUE.SHOW_COMPONENTS)) oapiWriteLog("VesselLoadMFD could not read default showComp!");

		oapiReadItem_bool(cfgFile, "ExperimentalParabolicArc", parabolicModeActive);
	}

	oapiCloseFile(cfgFile, FILE_IN_ZEROONFAIL);

	// Update the variables with our (potential) new defaults
	loadUnit = LOADUNIT(DEFAULT_VALUE.LOAD_UNIT);
	showText = DEFAULT_VALUE.SHOW_TEXT;
	showComponents = DEFAULT_VALUE.SHOW_COMPONENTS;
}


DLLCLBK void ExitModule(HINSTANCE hDLL)
{
	// Unregister the custom MFD mode when the module is unloaded
	oapiUnregisterMFDMode(g_MFDmode);

	oapiDelAnnotation(LoadText);
	oapiDelAnnotation(CompText);
	if (parabolicModeActive) oapiDelAnnotation(ParabolicText);

	delete[]loadData.time;
	delete[]loadData.load;
	delete[]loadData.loadToPlot;
	delete[]loadData.compX;
	delete[]loadData.compY;
	delete[]loadData.compZ;
	delete[]loadData.compXToPlot;
	delete[]loadData.compYToPlot;
	delete[]loadData.compZToPlot;
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

// ==============================================================
// MFD class implementation

// Constructor
LoadMFD::LoadMFD(DWORD w, DWORD h, VESSEL* vessel)
	: GraphMFD(w, h, vessel)
{
	int g;
	char yLabel[30];

	g = AddGraph(); // 0
	SetAxisTitle(g, 0, "Time: s");
	sprintf(yLabel, "Load: %s", GetLoadUnitName(loadUnit));
	SetAxisTitle(g, 1, yLabel);
	AddPlot(g, loadData.time, loadData.loadToPlot, NUM_POINTS, 1, &loadData.nextSample);

	// Graph with components
	g = AddGraph(); // 1
	SetAxisTitle(g, 0, "Time: s");
	sprintf(yLabel, "Load components: %s", GetLoadUnitName(loadUnit));
	SetAxisTitle(g, 1, yLabel);
	AddPlot(g, loadData.time, loadData.compXToPlot, NUM_POINTS, 2, &loadData.nextSample);
	AddPlot(g, loadData.time, loadData.compYToPlot, NUM_POINTS, 3, &loadData.nextSample);
	AddPlot(g, loadData.time, loadData.compZToPlot, NUM_POINTS, 5, &loadData.nextSample); // col 4 is dark yellow, skip, to get 5 = medium grey.
}

LoadMFD::~LoadMFD()
{
	oapiDelAnnotation(LoadText);
	oapiDelAnnotation(CompText);
	if (parabolicModeActive) oapiDelAnnotation(ParabolicText);
}

const int TOTAL_BUTTONS = 12;
// Return button labels
char* LoadMFD::ButtonLabel(int bt)
{
	// The labels for the two buttons used by our MFD mode
	static char* label[TOTAL_BUTTONS] = { "LOAD", "RST", "UNIT", "COMP", "RNG", " ", " ", " ", " ", " ", " ", "NULL" };
	if (parabolicModeActive) label[6] = "ARC";

	return (bt < TOTAL_BUTTONS ? label[bt] : 0);
}

// Return button menus
int LoadMFD::ButtonMenu(const MFDBUTTONMENU** menu) const
{
	// The menu descriptions for the two buttons
	static MFDBUTTONMENU mnu[TOTAL_BUTTONS] = {
		{ "Write load on screen", 0, 'L' },
		{ "Reset load", 0, 'R' },
		{ "Set load unit", 0, 'U' },
		{ "Show components", 0, 'C' },
		{ "Component plot range", 0, 'P' },
		{ " ", 0, ' ' },
		{ " ", 0, ' ' },
		{ " ", 0, ' ' },
		{ " ", 0, ' ' },
		{ " ", 0, ' ' },
		{ " ", 0, ' ' },
		{ "Reset all", 0, 'N' },
	};

	if (parabolicModeActive) mnu[6] = { "Parabolic flight mode", 0, 'A' };

	if (menu) *menu = mnu;
	return TOTAL_BUTTONS; // return the number of buttons used
}

DLLCLBK void opcPostStep(double simt, double simdt, double mjd)
{
	VESSEL* v = oapiGetFocusInterface();
	double m = v->GetMass();
	VECTOR3 F, W;
	v->GetForceVector(F);
	v->GetWeightVector(W);
	double AccX = (F.x - W.x) / m;
	double AccY = (F.y - W.y) / m;
	double AccZ = (F.z - W.z) / m;
	VesselAcceleration = sqrt(AccX * AccX + AccY * AccY + AccZ * AccZ);

	OBJHANDLE surfaceRef = v->GetSurfaceRef();

	if (v->GroundContact() && VesselAcceleration < 0.1)
	{
		VesselAcceleration = GGRAV * oapiGetMass(surfaceRef) / oapiGetSize(surfaceRef) / oapiGetSize(surfaceRef); // Orbiter bug gives zero acc on surface
	}

	LoadUnit = GetLoadUnitName(loadUnit);
	GFactor = GetLoadUnitFactor(loadUnit, surfaceRef);

	oapiDelAnnotation(LoadText);
	oapiDelAnnotation(CompText);
	if (parabolicModeActive) oapiDelAnnotation(ParabolicText);
	if (showText)
	{
		char LoadInfoText[20];
		sprintf(LoadInfoText, "%.2f %s", VesselAcceleration / GFactor, LoadUnit);
		LoadText = oapiCreateAnnotation(0, 1, _V(0, 1, 0));
		oapiAnnotationSetPos(LoadText, 0.9, 0.1, 1.0, 0.2);

		if (VesselAcceleration >= 6.0 * G) oapiAnnotationSetColour(LoadText, _V(1, 0, 0));
		else if (VesselAcceleration >= 4.0 * G) oapiAnnotationSetColour(LoadText, _V(1, 1, 0));
		else oapiAnnotationSetColour(LoadText, _V(0, 1, 0));

		oapiAnnotationSetText(LoadText, LoadInfoText);

		// Must show normal load to have option to show components
		if (showComponents)
		{
			char LoadInfoTextComp[60];

			sprintf(LoadInfoTextComp, "x: %+.2f %s, y: %+.2f %s, z: %+.2f %s", AccX / GFactor, LoadUnit, AccY / GFactor, LoadUnit, AccZ / GFactor, LoadUnit);
			CompText = oapiCreateAnnotation(0, 0.5, _V(0, 1, 0));
			oapiAnnotationSetPos(CompText, 0.85, 0.15, 1.0, 0.25);
			oapiAnnotationSetText(CompText, LoadInfoTextComp);
		}
	}

	if (simt < previousSimt || simt > previousSimt + 1e7) // if current time is before last time, i.e. we've had a hop back in time. Or if we're so far in future, that we want to reset.
	{
		// Reset
		memset(loadData.time, 0, NUM_POINTS * sizeof(float));
		loadData.nextTime = oapiGetSimTime();
		loadData.nextSample = 0;
		globalMaxLoad = 0;
		timeReset = oapiGetSimTime();
		memset(loadData.load, 0, NUM_POINTS * sizeof(float));
		memset(loadData.compX, 0, NUM_POINTS * sizeof(float));
		memset(loadData.compY, 0, NUM_POINTS * sizeof(float));
		memset(loadData.compZ, 0, NUM_POINTS * sizeof(float));
	}
	previousSimt = simt;

	if (simt >= loadData.nextTime)
	{
		loadData.time[loadData.nextSample] = float(simt - timeReset);
		loadData.load[loadData.nextSample] = float(VesselAcceleration);
		loadData.compX[loadData.nextSample] = float(AccX);
		loadData.compY[loadData.nextSample] = float(AccY);
		loadData.compZ[loadData.nextSample] = float(AccZ);

		loadData.nextSample = (loadData.nextSample + 1) % NUM_POINTS;
		loadData.nextTime = simt + SAMPLE_DT;

		if (globalMaxLoad < VesselAcceleration) globalMaxLoad = VesselAcceleration; // Save largest recorded value
	}

	if (parabolicModeActive && zeroGParabolicFlight)
	{
		double pitchLvl = 0.0, yawLvl = 0.0, powerLvl = 0.0;

		double targetAccY = 0.0 * G; // m/s^2

		{

			if (v->GetAltitude() < 5e3 || v->GetPitch() < -50 * RAD)
			{
				targetAccY = 2 * G;
				pullUp = true;
			}
			else if (v->GetAltitude() > 8e3 || v->GetPitch() > 50 * RAD)
			{
				targetAccY = 0.0;
				pullUp = false;
				if (v->GetAltitude() > 7e3)
				{
					thisTimeWeightless = 0.0;
					/*PIDintegralThrust = 0.0;
					PIDpreviousErrorThrust = 0.0;*/
				}
			}
			else if (pullUp)
			{
				targetAccY = 2 * G;
			}

			const double proportionalGainConstant = 0.04; // Kp
			const double integralGainConstant = 0.025; // Ki
			const double derivativeGainConstant = 0.01; // Kd
			double PIDerror = targetAccY - AccY; // setpoint - measured_value
			PIDintegral += PIDerror * simdt; // integral + error * dt
			double PIDderivative = (PIDerror - PIDpreviousError) / simdt;
			double PIDoutput = proportionalGainConstant * PIDerror + integralGainConstant * PIDintegral + derivativeGainConstant * PIDderivative;
			PIDpreviousError = PIDerror;
			pitchLvl = PIDoutput;
		}

		v->SetControlSurfaceLevel(AIRCTRL_ELEVATOR, pitchLvl);

		{

			const double proportionalGainConstant = 0.04; // Kp
			const double integralGainConstant = 0.025; // Ki
			const double derivativeGainConstant = 0.01; // Kd
			double PIDerror = 0.0 - AccX; // setpoint - measured_value
			PIDintegralYaw += PIDerror * simdt; // integral + error * dt
			double PIDderivative = (PIDerror - PIDpreviousErrorYaw) / simdt;
			double PIDoutput = proportionalGainConstant * PIDerror + integralGainConstant * PIDintegralYaw + derivativeGainConstant * PIDderivative;
			PIDpreviousErrorYaw = PIDerror;
			yawLvl = PIDoutput;
		}
		v->SetControlSurfaceLevel(AIRCTRL_RUDDER, yawLvl);

		double minEnergy = G * 9e3 + 0.5 * 100.0 * 100.0; // want to sustain 100 m/s at 9 km alt, 93.3 kJ/kg.
		double maxEnergy = G * 6e3 + 0.5 * 300.0 * 300.0; // max Mach 1 at bottom, 103.9 kg/kg
		double currentEnergy = G * v->GetAltitude() + 0.5 * v->GetGroundspeed() * v->GetGroundspeed();

		{
			double targetAccZ = 0.0 * G; // m/s^2

			if (currentEnergy < minEnergy) targetMaxEnergy = 1;
			else if (currentEnergy > maxEnergy) targetMaxEnergy = -1;

			if (!pullUp) targetMaxEnergy = 0;
			else if (targetMaxEnergy == 1 && currentEnergy > (minEnergy * 0.1 + maxEnergy * 0.9)) targetMaxEnergy = 0;
			else if (targetMaxEnergy == -1 && currentEnergy < (minEnergy * 0.1 + maxEnergy * 0.9)) targetMaxEnergy = 0; // mostly losing energy, not having too much

			//if (targetMaxEnergy == 1) targetAccZ = 0.8 * G;
			//else if (targetMaxEnergy == -1) targetAccZ = -0.8 * G;

			const double proportionalGainConstant = 0.02; // Kp
			const double integralGainConstant = 0.1; // Ki
			const double derivativeGainConstant = 0.000; // Kd
			double PIDerror = targetAccZ - AccZ; // setpoint - measured_value
			PIDintegralThrust += PIDerror * simdt; // integral + error * dt
			double PIDderivative = (PIDerror - PIDpreviousErrorThrust) / simdt;
			double PIDoutput = proportionalGainConstant * PIDerror + integralGainConstant * PIDintegralThrust + derivativeGainConstant * PIDderivative;
			PIDpreviousErrorThrust = PIDerror;
			powerLvl = PIDoutput;
			if (powerLvl < 0.0) powerLvl = 0.0;
			else if (powerLvl > 1.0) powerLvl = 1.0;
		}

		v->SetThrusterGroupLevel(THGROUP_MAIN, powerLvl);

		if (!pullUp && VesselAcceleration < 0.05 * G) timeWeightless += simdt;
		else if (!pullUp) timeWeightless = 0.0;

		// Comment 
		2 + 2;

		if (timeWeightless > thisTimeWeightless || timeWeightless > 5.0 || (thisTimeWeightless == 0.0 && !pullUp && v->GetAltitude() > 7e3)) thisTimeWeightless = timeWeightless;

		char *parabola = pullUp ? "Go up" : "ZeroG";

		sprintf(oapiDebugString(), "%s, timeWeightless: %.2f, pitch: %+.3f, yaw: %+.3f, power: %.3f, energy: %.3fk, happy (93-104): %i", parabola, thisTimeWeightless, pitchLvl, yawLvl, powerLvl, currentEnergy / 1e3, targetMaxEnergy);

		char parabolText[256];
		sprintf(parabolText, "AUTOMATIC mode!\n%s,\n timeWeightless: %.2f s", parabola, thisTimeWeightless);
		ParabolicText = oapiCreateAnnotation(0, 0.75, _V(0, 1, 0));
		oapiAnnotationSetPos(ParabolicText, 0.75, 0.2, 1.0, 0.8);
		oapiAnnotationSetText(ParabolicText, parabolText);
	}
	else if (parabolicModeActive) // i.e. "else" for when it is activated in cfg.
	{
		PIDintegral = 0.0;
		PIDpreviousError = 0.0;
		PIDintegralThrust = 0.0;
		PIDpreviousErrorThrust = 0.0;
		targetMaxEnergy = 0;
	}
}

// Draw MFD
void LoadMFD::Update(HDC hDC)
{
	char cbuf[256];
	sprintf(cbuf, "Load: %.2f %s  Peak: %.2f %s", VesselAcceleration / GFactor, LoadUnit, globalMaxLoad / GFactor, LoadUnit);
	if (parabolicModeActive && zeroGParabolicFlight) strcat(cbuf, ", ARC");
	Title(hDC, cbuf);

	float LoadMax, compLoadMax, TimeMin, TimeMax;

	FindRange(loadData.time, NUM_POINTS, TimeMin, TimeMax);

	if (TimeMin > TimeMax) TimeMax = TimeMin;
	else if (TimeMin == TimeMax) TimeMin -= 0.5, TimeMax += 0.5;

	if (TimeMax < (NUM_POINTS * SAMPLE_DT)) TimeMax = float(NUM_POINTS * SAMPLE_DT);

	TimeMin = float(TimeMax - NUM_POINTS * SAMPLE_DT);

	LoadMax = 0.0f;
	compLoadMax = 0.0f;

	for (int i = 0; i < NUM_POINTS; i++)
	{
		loadData.loadToPlot[i] = loadData.load[i] / GFactor;
		if (loadData.loadToPlot[i] > LoadMax && loadData.time[i] <  TimeMax && loadData.time[i] > TimeMin) LoadMax = loadData.loadToPlot[i];

		if (showComponents)
		{
			loadData.compXToPlot[i] = float(loadData.compX[i] / GFactor);
			loadData.compYToPlot[i] = float(loadData.compY[i] / GFactor);
			loadData.compZToPlot[i] = float(loadData.compZ[i] / GFactor);

			float currentMax = max(max(abs(loadData.compXToPlot[i]), abs(loadData.compYToPlot[i])), abs(loadData.compZToPlot[i]));
			if (currentMax > compLoadMax && loadData.time[i] <  TimeMax && loadData.time[i] > TimeMin) compLoadMax = currentMax;
		}
	}
	
	if (LoadMax < (G / GFactor)) LoadMax = float(G / GFactor);
	if (compLoadMax < (G / 2.0 / GFactor)) compLoadMax = float(G / 2.0 / GFactor); // divided by 2.0 as we're not plotting from 0, but symmetric about axis, and thus twice as much as normal load.

	SetRange(0, 0, TimeMin, TimeMax);
	SetRange(0, 1, 0, float(LoadMax + 0.2 * G / GFactor));

	char yLabel[20];
	sprintf(yLabel, "Load: %s", LoadUnit);
	SetAxisTitle(0, 1, yLabel);

	char plotTitle[30];
	sprintf(plotTitle, "Load (%s)", LoadUnit);
	if (showComponents) Plot(hDC, 0, ch, H / 2, plotTitle); // give space for coming graph
	else Plot(hDC, 0, ch, H - ch, plotTitle); // only display this graph

	if (showComponents)
	{
		// Display second graph
		SetRange(1, 0, TimeMin, TimeMax);
		if (componentRangeAuto) SetRange(1, 1, float(-compLoadMax - 0.2 * G / GFactor), float(compLoadMax + 0.2 * G / GFactor));
		else SetRange(1, 1, -componentRange, componentRange);
		sprintf(yLabel, "Load comp.: %s", LoadUnit);
		SetAxisTitle(1, 1, yLabel);
		sprintf(plotTitle, "Load comp. (%s)", LoadUnit);
		Plot(hDC, 1, H / 2, H - ch, plotTitle);

		// Add legend for components
		SetTextColor(hDC, RGB(0, 128, 0));
		sprintf(cbuf, "x");
		TextOut(hDC, 3 * W / 4 + 1 * W / 16, H / 2 - ch / 2, cbuf, strlen(cbuf));

		SetTextColor(hDC, RGB(128, 128, 0));
		sprintf(cbuf, "y");
		TextOut(hDC, 3 * W / 4 + 2 * W / 16, H / 2 - ch / 2, cbuf, strlen(cbuf));

		SetTextColor(hDC, RGB(128, 128, 128));
		sprintf(cbuf, "z");
		TextOut(hDC, 3 * W / 4 + 3 * W / 16, H / 2 - ch / 2, cbuf, strlen(cbuf));
	}
}

bool LoadMFD::ConsumeKeyBuffered(DWORD key)
{
	bool ComponentRange(void* id, char* str, void* data);

	switch (key)
	{
	case OAPI_KEY_L:
		showText = !showText;
		return true;
	case OAPI_KEY_R:
		memset(loadData.load, 0, NUM_POINTS * sizeof(float));
		memset(loadData.compX, 0, NUM_POINTS * sizeof(float));
		memset(loadData.compY, 0, NUM_POINTS * sizeof(float));
		memset(loadData.compZ, 0, NUM_POINTS * sizeof(float));
		loadData.nextTime = oapiGetSimTime();
		globalMaxLoad = 0.0;
		return true;
	case OAPI_KEY_U:
		loadUnit = LOADUNIT((int(loadUnit) + 1) % int(LOADLASTENTRY));
		return true;
	case OAPI_KEY_C:
		showComponents = !showComponents;
		return true;
	case OAPI_KEY_P:
		oapiOpenInputBox("Component plot range ('a' = auto):", ComponentRange, 0, 20, (void*)this);
		return true;
	case OAPI_KEY_A:
		if (parabolicModeActive)
		{
			zeroGParabolicFlight = !zeroGParabolicFlight;
			if (!zeroGParabolicFlight)
			{
				VESSEL* v = oapiGetFocusInterface();
				v->SetControlSurfaceLevel(AIRCTRL_ELEVATOR, 0.0);
				v->SetThrusterGroupLevel(THGROUP_ATT_PITCHUP, 0.0);
				v->SetThrusterGroupLevel(THGROUP_ATT_PITCHDOWN, 0.0);
				v->SetControlSurfaceLevel(AIRCTRL_RUDDER, 0.0);
				v->SetThrusterGroupLevel(THGROUP_MAIN, 1.0);
			}
			return true;
		}

		return false; // don't handle
	case OAPI_KEY_N:
		memset(loadData.time, 0, NUM_POINTS * sizeof(float));
		loadData.nextTime = oapiGetSimTime();
		loadData.nextSample = 0;
		globalMaxLoad = 0;
		timeReset = oapiGetSimTime();
		memset(loadData.load, 0, NUM_POINTS * sizeof(float));
		memset(loadData.compX, 0, NUM_POINTS * sizeof(float));
		memset(loadData.compY, 0, NUM_POINTS * sizeof(float));
		memset(loadData.compZ, 0, NUM_POINTS * sizeof(float));
		componentRangeAuto = true;
		return true;
	}
	return false;
}

bool ComponentRange(void* id, char* str, void* data)
{
	return ((LoadMFD*)data)->SetComponentsPlotRange(str);
}

bool LoadMFD::SetComponentsPlotRange(char* rstr)
{
	double inputValue = atof(rstr);

	if (!strcmp(rstr, "a") || !strcmp(rstr, "A")) // auto
	{
		componentRangeAuto = true;
		return true;
	}
	else if (inputValue > 0.0)
	{
		componentRange = inputValue;
		componentRangeAuto = false;
		return true;
	}
	return false;
}

bool LoadMFD::ConsumeButton(int bt, int event)
{
	if (!(event & PANEL_MOUSE_LBDOWN)) return false;

	if (bt == 0) return ConsumeKeyBuffered(OAPI_KEY_L);
	if (bt == 1) return ConsumeKeyBuffered(OAPI_KEY_R);
	if (bt == 2) return ConsumeKeyBuffered(OAPI_KEY_U);
	if (bt == 3) return ConsumeKeyBuffered(OAPI_KEY_C);
	if (bt == 4) return ConsumeKeyBuffered(OAPI_KEY_P);
	if (parabolicModeActive && bt == 6) return ConsumeKeyBuffered(OAPI_KEY_A);
	if (bt == 11) return ConsumeKeyBuffered(OAPI_KEY_N);
	else return false;
}

// MFD message parser
int LoadMFD::MsgProc(UINT msg, UINT mfd, WPARAM wparam, LPARAM lparam)
{
	switch (msg) {
	case OAPI_MSG_MFD_OPENED:
		// Our new MFD mode has been selected, so we create the MFD and
		// return a pointer to it.
		return (int)(new LoadMFD(LOWORD(wparam), HIWORD(wparam), (VESSEL*)lparam));
	}
	return 0;
}