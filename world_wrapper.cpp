#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <emscripten/val.h>
#include <emscripten/bind.h>
#include "audioio.h"
#include "world/d4c.h"
#include "world/dio.h"
#include "world/harvest.h"
#include "world/matlabfunctions.h"
#include "world/cheaptrick.h"
#include "world/stonemask.h"
#include "world/synthesis.h"
#include "world/synthesisrealtime.h"

#include "matlabfunctions.cpp"

#include <stdint.h>
#include <sys/time.h>

#define BASE_VERSION 1

using namespace std;
using namespace emscripten;

bool debug = false;

#ifndef DWORD
#define DWORD uint32_t
#endif
DWORD timeGetTime() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    DWORD ret = static_cast<DWORD>(tv.tv_usec / 1000 + tv.tv_sec * 1000);
    return ret;
}

struct ds_model {
	val f0;
	val time_axis;
	val spectral;
	val aperiodicity;
	int fs;
	int nbits;
	int frame_length;
	int fft_size;
};

template <class Type>
val get1XArray(Type *arr, int len){
	return val(typed_memory_view(len, arr));
}

template <class Type>
val get2XArray(Type **arr, int y_len, int x_len){
	val ret = val::array();
	for(int i = 0; i < y_len; i ++){
		ret.set(i, get1XArray<Type>(arr[i], x_len));
	}
	return ret;
}

double *generateTimeAxis(double frame_period, int frame_length){
	double *time_axis = new double[frame_length];
	double lastVal = 0;
	double timeStep = frame_period;
	for(int i = 0; i < frame_length; i ++){
		time_axis[i] = lastVal / 1000;
		lastVal += timeStep;
	}
	return time_axis;
}

template <class Type>
Type *getPtrFrom1XArray(val arr, int *len = NULL){
	if(len == NULL) len = new int[1];
	*len = arr["length"].as<int>();
	Type *ret = new Type[*len];
	val module = val::global("Module");
	int ptr = (int)ret / sizeof(Type);
	module["HEAPF64"].call<val>("set", arr, val(ptr));
	return ret;
}

template <class Type>
Type **getPtrFrom2XArray(val arr, int *y_len = NULL, int *x_len = NULL){
	if(y_len == NULL) x_len = new int[1];
	if(x_len == NULL) x_len = new int[1];
	*y_len = arr["length"].as<int>();

	val module = val::global("Module");
	int ptr;
	if(*y_len > 0){
		*x_len = arr[0]["length"].as<int>();
		Type **ret = new Type*[*y_len];
		for(int i = 0; i < *y_len; i ++){
			ret[i] = new Type[*x_len];
			ptr = (int)ret[i] / sizeof(Type);
			module["HEAPF64"].call<val>("set", arr[i], val(ptr));
		}
		return ret;
	} else {
		*x_len = 0;
		return NULL;
	}
}

val wavread_wrap(string filename){
	val ret = val::object();
	const char* fname = filename.c_str();
	int fs, nbit;
	int x_length = GetAudioLength(fname);
	double *x = new double[x_length];
	wavread(fname, &fs, &nbit, x);
	ret.set("x", get1XArray<double>(x, x_length));
	ret.set("fs", fs);
	ret.set("nbit", nbit);
	ret.set("x_length", x_length);
	delete [] x;
	return ret;
}

val dio_wrap(val x_val, int fs, double frame_period){
	val ret = val::object();
	int x_length;
	double *x = getPtrFrom1XArray<double>(x_val, &x_length);
	DioOption option = {0};
	InitializeDioOption(&option);
	option.frame_period = frame_period;
	option.speed = 1;
	option.f0_floor = 71.0;
	option.allowed_range = 0.2;
	int f0_length = GetSamplesForDIO(fs, x_length, frame_period);
	double *f0 = new double[f0_length];
	double *time_axis = new double[f0_length];
	Dio(x, x_length, fs, &option, time_axis, f0);
	ret.set("f0", get1XArray<double>(f0, f0_length));
	ret.set("time_axis", get1XArray<double>(time_axis, f0_length));
	delete [] f0;
	delete [] time_axis;
	delete [] x;
	return ret;
}

val harvest_wrap(val x_val, int fs, double frame_period){
	val ret = val::object();
	int x_length;
	double *x = getPtrFrom1XArray<double>(x_val, &x_length);
	HarvestOption option = {0};
	InitializeHarvestOption(&option);
	option.frame_period = frame_period;
	option.f0_floor = 71.0;
	int f0_length = GetSamplesForHarvest(fs, x_length, frame_period);
	double *f0 = new double[f0_length];
	double *time_axis = new double[f0_length];
	Harvest(x, x_length, fs, &option, time_axis, f0);
	ret.set("f0", get1XArray<double>(f0, f0_length));
	ret.set("time_axis", get1XArray<double>(time_axis, f0_length));
	delete [] f0;
	delete [] time_axis;
	delete [] x;
	return ret;
}

val cheapTrick_wrap(val x_val, val f0_val, val time_axis_val, int fs, double frame_period){
	val ret = val::object();
	int x_length, f0_length;
	double *x = getPtrFrom1XArray<double>(x_val, &x_length);
	double *f0 = getPtrFrom1XArray<double>(f0_val, &f0_length);
	double *time_axis = getPtrFrom1XArray<double>(time_axis_val);
	CheapTrickOption option = {0};
	InitializeCheapTrickOption(fs, &option);
	option.f0_floor = 71.0;
	option.fft_size = GetFFTSizeForCheapTrick(fs, &option);
	ret.set("fft_size", option.fft_size);
	double **spectrogram = new double *[f0_length];
	int specl = option.fft_size / 2 + 1;
	for (int i = 0; i < f0_length; i++) spectrogram[i] = new double[specl];
	CheapTrick(x, x_length, fs, time_axis, f0, f0_length, &option, spectrogram);
	ret.set("spectral", get2XArray<double>(spectrogram, f0_length, specl));
	delete [] x;
	delete [] f0;
	delete [] time_axis;
	delete [] spectrogram;
	return ret;
}

val d4c_wrap(val x_val, val f0_val, val time_axis_val, int fft_size, int fs, double frame_period){
	val ret = val::object();
	int x_length, f0_length;
	double *x = getPtrFrom1XArray<double>(x_val, &x_length);
	double *f0 = getPtrFrom1XArray<double>(f0_val, &f0_length);
	double *time_axis = getPtrFrom1XArray<double>(time_axis_val);
	D4COption option = {0};
	InitializeD4COption(&option);
	option.threshold = 0.85;
	double **aperiodicity = new double *[f0_length];
	int specl = fft_size / 2 + 1;
	for (int i = 0; i < f0_length; ++i) aperiodicity[i] = new double[specl];
	D4C(x, x_length, fs, time_axis, f0, f0_length, fft_size, &option, aperiodicity);
	ret.set("aperiodicity", get2XArray<double>(aperiodicity, f0_length, specl));
	delete [] x;
	delete [] f0;
	delete [] time_axis;
	delete [] aperiodicity;
	return ret;
}

val buildModel(string filename, double frame_period = 2.0){
	val ret = val::object();
	//pre
	ret.set("frame_period", frame_period);
	const char* fname = filename.c_str();
	int fs, nbit;
	int x_length = GetAudioLength(fname);
	double *x = new double[x_length];
	DWORD elapsed_time;
	wavread(fname, &fs, &nbit, x);
	if(debug) cout << "Length: " << static_cast<double>(x_length) / fs <<  "[sec]" << endl;
	ret.set("fs", fs);
	ret.set("nbit", nbit);
	//dio
	DioOption optionDio = {0};
	InitializeDioOption(&optionDio);
	optionDio.frame_period = frame_period;
	optionDio.speed = 1;
	optionDio.f0_floor = 71.0;
	optionDio.allowed_range = 0.2;
	if(debug) elapsed_time = timeGetTime();
	int f0_length = GetSamplesForDIO(fs, x_length, frame_period);
	if(debug) cout << "DIO: " << timeGetTime() - elapsed_time << " [msec]" << endl;
	double *f0 = new double[f0_length];
	double *time_axis = new double[f0_length];
	Dio(x, x_length, fs, &optionDio, time_axis, f0);
	ret.set("frame_length", f0_length);
	//ret.set("time_axis", get1XArray<double>(time_axis, f0_length));
	//spectral
	CheapTrickOption optionSpec = {0};
	InitializeCheapTrickOption(fs, &optionSpec);
	optionSpec.f0_floor = 71.0;
	optionSpec.fft_size = GetFFTSizeForCheapTrick(fs, &optionSpec);
	int fft_size = optionSpec.fft_size;
	ret.set("fft_size", fft_size);

	//spectral envelop
	double **spectrogram = new double *[f0_length];
	int specl = optionSpec.fft_size / 2 + 1;
	if(debug) elapsed_time = timeGetTime();
	for (int i = 0; i < f0_length; i++) spectrogram[i] = new double[specl];
	CheapTrick(x, x_length, fs, time_axis, f0, f0_length, &optionSpec, spectrogram);
	if(debug) cout << "CheapTrick: " << timeGetTime() - elapsed_time << " [msec]" << endl;

	//aperiodicity
	D4COption optionD4c = {0};
	InitializeD4COption(&optionD4c);
	optionD4c.threshold = 0.85;
	double **aperiodicity = new double *[f0_length];
	for (int i = 0; i < f0_length; ++i) aperiodicity[i] = new double[specl];
	if(debug) elapsed_time = timeGetTime();
	D4C(x, x_length, fs, time_axis, f0, f0_length, fft_size, &optionD4c, aperiodicity);
	if(debug) cout << "D4C: " << timeGetTime() - elapsed_time << " [msec]" << endl;
	//set data
	if(debug) elapsed_time = timeGetTime();
	ret.set("f0", get1XArray<double>(f0, f0_length));
	ret.set("spectral", get2XArray<double>(spectrogram, f0_length, specl));
	ret.set("aperiodicity", get2XArray<double>(aperiodicity, f0_length, specl));
	if(debug) cout << "Convert data: " << timeGetTime() - elapsed_time << " [msec]" << endl;
	//free memory
	delete [] x;
	delete [] f0;
	delete [] time_axis;
	delete [] spectrogram;
	delete [] aperiodicity;
	return ret;
}

val synthesis_wrap(val f0_val, val spectral_val, val aperiodicity_val, int fft_size, int fs, val frame_period){
	int f0_length;
	DWORD elapsed_time;
	if(debug) elapsed_time = timeGetTime();
	bool useDynamicFP = false;
	double *framePeriodVals;
	double framePeriodVal;
	if(frame_period.typeof().as<string>() == "object"){
		framePeriodVals = getPtrFrom1XArray<double>(frame_period);
		useDynamicFP = true;
	} else {
		framePeriodVal = frame_period.as<double>();
	}
	double *f0 = getPtrFrom1XArray<double>(f0_val, &f0_length);
	
	double **spectrogram = getPtrFrom2XArray<double>(spectral_val);
	double **aperiodicity = getPtrFrom2XArray<double>(aperiodicity_val);
	int y_length;
	if(useDynamicFP){
		y_length = getDynamicYLength(f0_length, framePeriodVals, fs);
	} else {
		y_length = static_cast<int>((f0_length - 1) * framePeriodVal / 1000.0 * fs) + 1;
	}
	double *y = new double[y_length];

	if(debug) cout << "Convert data: " << timeGetTime() - elapsed_time << " [msec]" << endl;
	
	if(debug) elapsed_time = timeGetTime();
	if(useDynamicFP){
		SynthesisDynamicSpeed(f0, f0_length, spectrogram, aperiodicity, fft_size, framePeriodVals, fs, y_length, y);
	} else {
		Synthesis(f0, f0_length, spectrogram, aperiodicity, fft_size, framePeriodVal, fs, y_length, y);
	}
	if(debug) cout << "WORLD: " << timeGetTime() - elapsed_time << " [msec]" << endl;
	if(useDynamicFP) delete[] framePeriodVals;
	delete [] f0;
	delete [] spectrogram;
	delete [] aperiodicity;
	val ret = get1XArray<double>(y, y_length);
	delete [] y;
	return ret;
}

val mSynthesis(val model){
	string requireList[6] = {"f0", "spectral", "aperiodicity", "fft_size", "fs", "frame_period"};
	for(int i = 0; i < 6; i ++){
		if(!model[requireList[i]].as<bool>()){
			return val("Error: undefined: " + requireList[i]);
		}
	}
	return synthesis_wrap(model["f0"], model["spectral"], model["aperiodicity"], model["fft_size"].as<int>(), model["fs"].as<int>(), model["frame_period"]);
}

val synthesisSave_wrap(val f0_val, val spectral_val, val aperiodicity_val, int fft_size, int fs, int nbits, val frame_period, string filename){
	int f0_length;

	DWORD elapsed_time;
	if(debug) elapsed_time = timeGetTime();
	bool useDynamicFP = false;
	double *framePeriodVals;
	double framePeriodVal;
	if(frame_period.typeof().as<string>() == "object"){
		framePeriodVals = getPtrFrom1XArray<double>(frame_period);
		useDynamicFP = true;
	} else {
		framePeriodVal = frame_period.as<double>();
	}
	double *f0 = getPtrFrom1XArray<double>(f0_val, &f0_length);
	double **spectrogram = getPtrFrom2XArray<double>(spectral_val);
	double **aperiodicity = getPtrFrom2XArray<double>(aperiodicity_val);
	
	int y_length;
	if(useDynamicFP){
		y_length = getDynamicYLength(f0_length, framePeriodVals, fs);
	} else {
		y_length = static_cast<int>((f0_length - 1) * framePeriodVal / 1000.0 * fs) + 1;
	}
	double *y = new double[y_length];

	if(debug) cout << "Convert data: " << timeGetTime() - elapsed_time << " [msec]" << endl;

	if(debug) elapsed_time = timeGetTime();
	if(useDynamicFP){
		SynthesisDynamicSpeed(f0, f0_length, spectrogram, aperiodicity, fft_size, framePeriodVals, fs, y_length, y);
	} else {
		Synthesis(f0, f0_length, spectrogram, aperiodicity, fft_size, framePeriodVal, fs, y_length, y);
	}
	if(debug) cout << "WORLD: " << timeGetTime() - elapsed_time << " [msec]" << endl;
	if(debug) elapsed_time = timeGetTime();
	wavwrite(y, y_length, fs, nbits, filename.c_str());
	if(debug) cout << "Write file: " << timeGetTime() - elapsed_time << " [msec]" << endl;
	delete [] f0;
	delete [] spectrogram;
	delete [] aperiodicity;
	delete [] y;
	return val(y_length);
}

val mSynthesisSave(val model, string filename){
	string requireList[7] = {"f0", "spectral", "aperiodicity", "fft_size", "fs", "nbit", "frame_period"};
	for(int i = 0; i < 7; i ++){
		if(!model[requireList[i]].as<bool>()){
			return val("Error: undefined: " + requireList[i]);
		}
	}
	return synthesisSave_wrap(model["f0"], model["spectral"], model["aperiodicity"], model["fft_size"].as<int>(), model["fs"].as<int>(), model["nbit"].as<int>(), model["frame_period"], filename);
}

val wavwrite_wrap(val y_val, int fs, int nbits, string filename){
	int y_length;
	double *y = getPtrFrom1XArray<double>(y_val, &y_length);
	wavwrite(y, y_length, fs, 16, filename.c_str());
	return val(y_length);
}

val saveModel(val f0_val, val time_axis_val, val spectral_val, val aperiodicity_val, int fft_size, int fs, int nbits, double frame_period, string filename){
	//header
	streamsize intSize = streamsize(sizeof(int));
	streamsize doubleSize = streamsize(sizeof(double));
	
	int f0_length;
	double *f0 = getPtrFrom1XArray<double>(f0_val, &f0_length);
	double *time_axis = getPtrFrom1XArray<double>(time_axis_val);
	
	double **spectrogram = getPtrFrom2XArray<double>(spectral_val);
	double **aperiodicity = getPtrFrom2XArray<double>(aperiodicity_val);
	
	int specl = fft_size / 2 + 1;
	
	ofstream out(filename,  ios::out | ios::binary);
	//identity
	out << "dualspace";
	//version
	int version = BASE_VERSION;
	out.write(reinterpret_cast<const char*>(&version), intSize);
	//fs
	out.write(reinterpret_cast<const char*>(&fs), intSize);
	//nbits
	out.write(reinterpret_cast<const char*>(&nbits), intSize);
	//frame_period
	out.write(reinterpret_cast<const char*>(&frame_period), doubleSize);
	//frame_length
	out.write(reinterpret_cast<const char*>(&f0_length), intSize);
	//fft_size
	out.write(reinterpret_cast<const char*>(&fft_size), intSize);
	for(int i = 0; i < f0_length; i ++){
		//f0
		out.write(reinterpret_cast<const char*>(&f0[i]), doubleSize);
		//spectral
		out.write(reinterpret_cast<const char*>(spectrogram[i]), streamsize(specl * sizeof(double)));
		//aperiodicity
		out.write(reinterpret_cast<const char*>(aperiodicity[i]), streamsize(specl * sizeof(double)));
	}
	out.close();
	delete [] f0;
	delete [] time_axis;
	delete [] spectrogram;
	delete [] aperiodicity;
	return val(f0_length);
}

val mSaveModel(val model, string filename){
	string requireList[8] = {"f0", "time_axis", "spectral", "aperiodicity", "fft_size", "fs", "nbit", "frame_period"};
	for(int i = 0; i < 8; i ++){
		if(!model[requireList[i]].as<bool>()){
			return val("Error: undefined: " + requireList[i]);
		}
	}
	return saveModel(model["f0"], model["time_axis"], model["spectral"], model["aperiodicity"], model["fft_size"].as<int>(), model["fs"].as<int>(), model["nbit"].as<int>(), model["frame_period"].as<double>(), filename);
}

val loadModel(string filename, int start = 0, int length = 0){
	val ret = val::object();
	ifstream m(filename,  ios::in | ios::binary);
	if(!m.is_open()){
		ret.set("error", 1000);
		ret.set("desc", "file not exists");
		ret.set("message", "Error: cannot open file: " + filename);
		ret.set("file", filename);
		return ret;
	}
	string identity;
	m >> setw(9) >> identity;
	if(identity != "dualspace"){
		ret.set("error", 1001);
		ret.set("desc", "unknow file type");
		ret.set("message", "Error: unknow file type: " + filename);
		ret.set("file", filename);
		return ret;
	}
	streamsize intSize = streamsize(sizeof(int));
	streamsize doubleSize = streamsize(sizeof(double));
	
	int version, fs, nbits, frame_length, fft_size;
	double frame_period;
	
	//m.seekg(9, ios::beg);
	
	m.read(reinterpret_cast<char*>(&version), intSize);
	if(version != BASE_VERSION){
		ret.set("error", 1002);
		ret.set("desc", "unmatch model version");
		stringstream msg;
		msg << "Error: unmatch model version: " << version << " (now version: " << (int)BASE_VERSION << ")";
		ret.set("message", msg.str());
		ret.set("version", version);
		return ret;
	}
	m.read(reinterpret_cast<char*>(&fs), intSize);
	m.read(reinterpret_cast<char*>(&nbits), intSize);
	m.read(reinterpret_cast<char*>(&frame_period), doubleSize);
	m.read(reinterpret_cast<char*>(&frame_length), intSize);
	m.read(reinterpret_cast<char*>(&fft_size), intSize);
	
	ret.set("fs", fs);
	ret.set("nbit", nbits);
	ret.set("frame_period", frame_period);
	
	ret.set("fft_size", fft_size);
	
	//fix start
	start = max(0, min(start, frame_length));
	if(length == 0) length = frame_length - start;
	//fix length
	length = max(0, min(frame_length - start, length));
	
	ret.set("frame_length", length);
	
	//start memory alloc
	double *f0 = new double[length];
	
	double **spectrogram = new double*[length];
	double **aperiodicity = new double*[length];
	
	int specl = fft_size / 2 + 1;
	
	m.seekg((sizeof(double) * 2 + sizeof(double) * specl * 2) * start, ios::cur);
	//load model data
	for(int i = 0; i < length; i ++){
		m.read(reinterpret_cast<char*>(&f0[i]), doubleSize);
		
		spectrogram[i] = new double[specl];
		m.read(reinterpret_cast<char*>(spectrogram[i]), streamsize(specl * sizeof(double)));
		
		aperiodicity[i] = new double[specl];
		m.read(reinterpret_cast<char*>(aperiodicity[i]), streamsize(specl * sizeof(double)));
	}
	
	m.close();
	
	//generate time_axis
	double *time_axis = generateTimeAxis(frame_period, length);
	ret.set("f0", get1XArray<double>(f0, length));
	ret.set("time_axis", get1XArray<double>(time_axis, length));
	ret.set("spectral", get2XArray<double>(spectrogram, length, specl));
	ret.set("aperiodicity", get2XArray<double>(aperiodicity, length, specl));
	
	delete [] f0;
	delete [] time_axis;
	delete [] spectrogram;
	delete [] aperiodicity;
	return ret;
}

val getFormants(val spec_val){
	int frame_length, specl;
	double **spec = getPtrFrom2XArray<double>(spec_val, &frame_length, &specl);
	double *t1 = new double[specl];
	double *t2 = new double[specl];
	int **formants = new int*[4];
	int c;
	for(int i = 0; i < 4; i ++){
		formants[i] = new int[frame_length];
	}
	for(int i = 0; i < frame_length; i ++){
		diff(spec[i], specl, t1);
		sign(t1, specl, t2);
		diff(t2, specl, t1);
		c = 0;
		for(int j = 0; j < specl && c < 4; j ++){
			if(t1[j] < 0){
				formants[c][i] = j;
				c ++;
			}
		}
		for(; c < 4; c ++){
			formants[c][i] = 0;
		}
	}
	val ret = get2XArray<int>(formants, 4, frame_length);
	delete [] spec;
	delete [] t1;
	delete [] t2;
	delete [] formants;
	return ret;
}

void setDebug(bool mode){
	debug = mode;
}

EMSCRIPTEN_BINDINGS(my_module) {
    emscripten::function("wavread", &wavread_wrap);
    emscripten::function("dio", &dio_wrap);
	emscripten::function("harvest", &harvest_wrap);
    emscripten::function("cheapTrick", &cheapTrick_wrap);
    emscripten::function("d4c", &d4c_wrap);
    emscripten::function("buildModel", &buildModel);
    emscripten::function("synthesis", &synthesis_wrap);
    emscripten::function("mSynthesis", &mSynthesis);
    emscripten::function("synthesisSave", &synthesisSave_wrap);
    emscripten::function("mSynthesisSave", &mSynthesisSave);
    emscripten::function("wavwrite", &wavwrite_wrap);
    emscripten::function("saveModel", &saveModel);
    emscripten::function("mSaveModel", &mSaveModel);
    emscripten::function("loadModel", &loadModel);
	emscripten::function("setDebug", &setDebug);
	emscripten::function("getFormants", &getFormants);
}
