/**
 *  @file SpeciesThermoFactory.cpp
 */

/*
 * $Author$
 * $Revision$
 * $Date$
 */

// Copyright 2001  California Institute of Technology


#ifdef WIN32
#pragma warning(disable:4786)
#endif

#include "SpeciesThermoFactory.h"

#include "SpeciesThermo.h"
#include "NasaThermo.h"
#include "ShomateThermo.h"
#include "PolyThermoMgr.h"
#include "SimpleThermo.h"

#include "SpeciesThermoMgr.h"
#include "speciesThermoTypes.h"

#include "xml.h"
#include "ctml.h"
using namespace ctml;

namespace Cantera {

    SpeciesThermoFactory* SpeciesThermoFactory::__factory = 0;


    static void getSpeciesThermoTypes(XML_Node* node, 
        int& has_nasa, int& has_shomate, int& has_simple) {
        const XML_Node& sparray = *node;
        vector<XML_Node*> sp;
        sparray.getChildren("species",sp);
        int ns = sp.size();
        for (int n = 0; n < ns; n++) {
	  XML_Node* spNode = sp[n];
	  if (spNode->hasChild("thermo")) {
            const XML_Node& th = sp[n]->child("thermo");
            if (th.hasChild("NASA")) has_nasa = 1;
            if (th.hasChild("Shomate")) has_shomate = 1;
            if (th.hasChild("const_cp")) has_simple = 1;
            if (th.hasChild("poly")) {
                if (th.child("poly")["order"] == "1") has_simple = 1;
                else throw CanteraError("newSpeciesThermo",
                    "poly with order > 1 not yet supported");
            }
	  } else {
	    throw UnknownSpeciesThermoModel("getSpeciesThermoTypes:",
					    spNode->attrib("name"), "missing");
	  }
        }
    }


    /**
     * Return a species thermo manager to handle the parameterizations
     * specified in a CTML phase specification.
     */
    SpeciesThermo* SpeciesThermoFactory::newSpeciesThermo(XML_Node* node) {
        int inasa = 0, ishomate = 0, isimple = 0;
        getSpeciesThermoTypes(node, inasa, ishomate, isimple);
        return newSpeciesThermo(NASA*inasa
            + SHOMATE*ishomate + SIMPLE*isimple);
    }

    SpeciesThermo* SpeciesThermoFactory::newSpeciesThermo(vector<XML_Node*> nodes) {
        int n = nodes.size();
        int inasa = 0, ishomate = 0, isimple = 0;
        for (int j = 0; j < n; j++) {
            getSpeciesThermoTypes(nodes[j], inasa, ishomate, isimple);
        }
        return newSpeciesThermo(NASA*inasa
            + SHOMATE*ishomate + SIMPLE*isimple);
    }


    SpeciesThermo* SpeciesThermoFactory::newSpeciesThermoOpt(vector<XML_Node*> nodes) {
        int n = nodes.size();
        int inasa = 0, ishomate = 0, isimple = 0;
        for (int j = 0; j < n; j++) {
	  try {
            getSpeciesThermoTypes(nodes[j], inasa, ishomate, isimple);
	  } catch (UnknownSpeciesThermoModel) {

	  }
        }
        return newSpeciesThermo(NASA*inasa
            + SHOMATE*ishomate + SIMPLE*isimple);
    }


    SpeciesThermo* SpeciesThermoFactory::newSpeciesThermo(int type) {
        
        switch (type) {

        case NASA:
            return new NasaThermo;
        case SHOMATE:
            return new ShomateThermo;
        case SIMPLE:
            return new SimpleThermo;
        case NASA + SHOMATE:
            return new SpeciesThermoDuo<NasaThermo, ShomateThermo>;
        case NASA + SIMPLE:
            return new SpeciesThermoDuo<NasaThermo, SimpleThermo>;
        default:
            throw UnknownSpeciesThermo(
                "SpeciesThermoFactory::newSpeciesThermo",type);
            return 0; 
        }
    }


        /// Check the continuity of properties at the midpoint
        /// temperature, and adjust the high-T coefficients to
        /// make the properties exactly continuous at Tmid.
    void NasaThermo::checkContinuity(double tmid, const doublereal* clow,
            doublereal* chigh) {

            // heat capacity
            doublereal cplow = poly4(tmid, clow+2);
            doublereal cphigh = poly4(tmid, chigh+2);
            doublereal delta = cplow - cphigh;
            if (fabs(delta/cplow) > 0.001) {
                writelog("\n**** WARNING ****\nDiscontinuity in cp/R detected at Tmid = "
                    +fp2str(tmid)+"\n");
                writelog("\tValue computed using low-temperature polynomial:  "+fp2str(cplow)+".\n");
                writelog("\tValue computed using high-temperature polynomial: "+fp2str(cphigh)+".\n");
            }
            //chigh[2] += cplow - cphigh;

            // enthalpy
            doublereal hrtlow = enthalpy_RT(tmid, clow);
            doublereal hrthigh = enthalpy_RT(tmid, chigh);
            delta = hrtlow - hrthigh;
            //            chigh[0] += tmid*(hrtlow - hrthigh);
            if (fabs(delta/hrtlow) > 0.001) {
                writelog("\n**** WARNING ****\nDiscontinuity in h/RT detected at Tmid = "
                    +fp2str(tmid)+"\n");
                writelog("\tValue computed using low-temperature polynomial:  "+fp2str(hrtlow)+".\n");
                writelog("\tValue computed using high-temperature polynomial: "+fp2str(hrthigh)+".\n");
            }

            // entropy
            doublereal srlow = entropy_R(tmid, clow);
            doublereal srhigh = entropy_R(tmid, chigh);
            //chigh[1] += srlow - srhigh;
            delta = srlow - srhigh;
            if (fabs(delta/srlow) > 0.001) {
                writelog("\n**** WARNING ****\nDiscontinuity in s/R detected at Tmid = "
                    +fp2str(tmid)+"\n");
                writelog("\tValue computed using low-temperature polynomial:  "+fp2str(srlow)+".\n");
                writelog("\tValue computed using high-temperature polynomial: "+fp2str(srhigh)+".\n");
            }
        }


    /** 
     * Install a NASA polynomial thermodynamic property
     * parameterization for species k into a SpeciesThermo instance.
     */
    static void installNasaThermoFromXML(SpeciesThermo& sp, int k, 
        const XML_Node* f0ptr, const XML_Node* f1ptr) {
        doublereal tmin0, tmax0, tmin1, tmax1, tmin, tmid, tmax;

        const XML_Node& f0 = *f0ptr;
        bool dualRange = false;
        if (f1ptr) {dualRange = true;}
        tmin0 = fpValue(f0["Tmin"]);
        tmax0 = fpValue(f0["Tmax"]);
        tmin1 = tmax0;
        tmax1 = tmin1 + 0.0001;
        if (dualRange) {
            tmin1 = fpValue((*f1ptr)["Tmin"]);
            tmax1 = fpValue((*f1ptr)["Tmax"]);
        }

        vector_fp c0, c1;
        if (fabs(tmax0 - tmin1) < 0.01) {
            tmin = tmin0;
            tmid = tmax0;
            tmax = tmax1;
            getFloatArray(f0.child("floatArray"), c0, false);
            if (dualRange)
                getFloatArray(f1ptr->child("floatArray"), c1, false);
            else
                c1.resize(7,0.0);
        }
        else if (fabs(tmax1 - tmin0) < 0.01) {
            tmin = tmin1;
            tmid = tmax1;
            tmax = tmax0;
            getFloatArray(f1ptr->child("floatArray"), c0, false);
            getFloatArray(f0.child("floatArray"), c1, false);
        }
        else {
            throw CanteraError("installNasaThermo",
			       "non-continuous temperature ranges.");
        }
        array_fp c(15);
        c[0] = tmid;
        doublereal p0 = OneAtm;
        c[1] = c0[5];
        c[2] = c0[6];
        copy(c0.begin(), c0.begin()+5, c.begin() + 3);
        c[8] = c1[5];
        c[9] = c1[6];
        copy(c1.begin(), c1.begin()+5, c.begin() + 10);
        sp.install(k, NASA, c.begin(), tmin, tmax, p0);
    }


    /** 
     * Install a NASA polynomial thermodynamic property
     * parameterization for species k.
     */
    static void installShomateThermoFromXML(SpeciesThermo& sp, int k, 
        const XML_Node* f0ptr, const XML_Node* f1ptr) {
        doublereal tmin0, tmax0, tmin1, tmax1, tmin, tmid, tmax;

        const XML_Node& f0 = *f0ptr;
        bool dualRange = false;
        if (f1ptr) {dualRange = true;}
        tmin0 = fpValue(f0["Tmin"]);
        tmax0 = fpValue(f0["Tmax"]);
        tmin1 = tmax0;
        tmax1 = tmin1 + 0.0001;
        if (dualRange) {
            tmin1 = fpValue((*f1ptr)["Tmin"]);
            tmax1 = fpValue((*f1ptr)["Tmax"]);
        }

        vector_fp c0, c1;
        if (fabs(tmax0 - tmin1) < 0.01) {
            tmin = tmin0;
            tmid = tmax0;
            tmax = tmax1;
            getFloatArray(f0.child("floatArray"), c0, false);
            if (dualRange)
                getFloatArray(f1ptr->child("floatArray"), c1, false);
            else
                c1.resize(7,0.0);
        }
        else if (fabs(tmax1 - tmin0) < 0.01) {
            tmin = tmin1;
            tmid = tmax1;
            tmax = tmax0;
            getFloatArray(f1ptr->child("floatArray"), c0, false);
            getFloatArray(f0.child("floatArray"), c1, false);
        }
        else {
            throw CanteraError("installShomateThermo",
			       "non-continuous temperature ranges.");
        }
        array_fp c(15);
        c[0] = tmid;
        doublereal p0 = OneAtm;
        copy(c0.begin(), c0.begin()+7, c.begin() + 1);
        copy(c1.begin(), c1.begin()+7, c.begin() + 8);
        sp.install(k, SHOMATE, c.begin(), tmin, tmax, p0);
    }



    /** 
     * Install a constant-cp thermodynamic property
     * parameterization for species k.
     */
    static void installSimpleThermoFromXML(SpeciesThermo& sp, int k, 
        const XML_Node& f) {
        doublereal tmin, tmax;
        tmin = fpValue(f["Tmin"]);
        tmax = fpValue(f["Tmax"]);
        if (tmax == 0.0) tmax = 1.0e30;

        vector_fp c(4);
        c[0] = getFloat(f, "t0", "-");
        c[1] = getFloat(f, "h0", "-");
        c[2] = getFloat(f, "s0", "-");
        c[3] = getFloat(f, "cp0", "-");
        doublereal p0 = OneAtm;
        sp.install(k, SIMPLE, c.begin(), tmin, tmax, p0);
    }


    /**
     * Install a species thermodynamic property parameterization
     * for one species into a species thermo manager.
     * @param k species number
     * @param s XML node specifying species
     * @param spthermo species thermo manager
     */
    void SpeciesThermoFactory::
    installThermoForSpecies(int k, const XML_Node& s, 
        SpeciesThermo& spthermo) {

	const XML_Node& thermo = s.child("thermo");
	const vector<XML_Node*>& tp = thermo.children();
	int nc = tp.size();
	if (nc == 1) {
            const XML_Node* f = tp[0];
            if (f->name() == "Shomate") {
                installShomateThermoFromXML(spthermo, k, f, 0);
            }
            else if (f->name() == "const_cp") {
                installSimpleThermoFromXML(spthermo, k, *f);
            }
            else if (f->name() == "NASA") {
                installNasaThermoFromXML(spthermo, k, f, 0);
            }
            else {
                UnknownSpeciesThermoModel("installSpecies", s["name"], f->name());
            }
	}
	else if (nc == 2) {
            const XML_Node* f0 = tp[0];
            const XML_Node* f1 = tp[1];
            if (f0->name() == "NASA" && f1->name() == "NASA") {
                installNasaThermoFromXML(spthermo, k, f0, f1);
            } 
            else if (f0->name() == "Shomate" && f1->name() == "Shomate") {
                installShomateThermoFromXML(spthermo, k, f0, f1);
            } 
            else {
                UnknownSpeciesThermoModel("installSpecies", s["name"], 
                    f0->name() + " and " + f1->name());
            }
	}
	else {
	    UnknownSpeciesThermoModel("installSpecies", s["name"], 
                "multiple");
	}
    }

}
