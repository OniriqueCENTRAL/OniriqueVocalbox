#pragma once

#include "AmpStage.h"
#include "Transformer.h"

namespace n73
{
//==============================================================================
//  Étage de sortie BA283 AM : 2N3055 (BDY61) en classe A à un seul transistor, dont la charge est le primaire du
//  transformateur LO1166 (à entrefer, rapport 1:1,7, traversé par le courant de repos). Tensions rapportées au
//  secondaire, charge de 600 Ω (conditions de la fiche AMS).
//
//  Ce qui fait son caractère, d'après le schéma EX10283 :
//  - le transistor ne peut que réduire son courant pour faire monter la sortie : de ce côté, la sortie bute quand le
//    courant demandé (charge + courant magnétisant du transformateur) atteint le courant de repos — butée douce
//    (blocage progressif du transistor), plus tôt dans le grave où le courant magnétisant s'ajoute ;
//  - de l'autre côté, la saturation du transistor : butée plus dure et plus haute ;
//  - le LO1166 lui-même (noyau polarisé, voir Transformer) : harmoniques paires qui montent vers le grave.
//
//  vCut : sortie crête au blocage, à fréquence moyenne (+28 dBu mesuré par SOS sur le 1073N → 27,5 V crête).
//==============================================================================
class OutputStage
{
public:
    struct Params
    {
        double gain = 9.87;                 // gain de l'étage, transformateur compris (19,9 dB)
        double hd2 = 3.0e-5, hd3 = 3.0e-4;  // aux petits signaux, à vRef (courbure du transistor, sous contre-réaction)
        double vRef = 10.95;                // +20 dBu crête
        double vCut = 27.5, vSat = 33.0;    // butées (V crête au secondaire)
        double load = 600.0;                // charge (Ω)
        Transformer::Params xf;             // LO1166
    };

    void prepare (double fs)
    {
        poly.prepare (fs);
        xf.prepare (fs);
        setParams (P);
    }
    void reset() { poly.reset(); xf.reset(); }

    void setParams (const Params& p)
    {
        P = p;
        AmpStage::Params ap;
        ap.gain = 1.0;
        ap.hd2 = P.hd2; ap.hd3 = P.hd3; ap.vRef = P.vRef;
        ap.vPos = 1.0e6; ap.vNeg = 1.0e6;   // les butées sont traitées ici
        ap.fEmph = 7000.0; ap.emph = 3.0; ap.fCouple = 0.5;
        poly.setParams (ap);
        xf.setParams (P.xf);
        iCut = P.vCut / P.load;
        invSat = 1.0 / P.vSat;
    }

    void setGain (double g) noexcept { P.gain = g; }

    /** dérive de la sortie : 0 = repos, 1 = au blocage (pour le témoin). */
    double lastDrive() const noexcept { return drive; }

    inline double process (double x) noexcept
    {
        const double vt = poly.process (x * P.gain);
        // courant demandé au transistor (rapporté au secondaire) : charge + courant magnétisant du LO1166
        const double iReq = vt / P.load + xf.magnetizingCurrent();
        // côté blocage : butée sur le courant ; la contre-réaction garde l'étage linéaire presque jusqu'au blocage
        double iAct = iReq;
        if (iReq > 0.0)
        {
            const double t = iReq / iCut, t2 = t * t, t4 = t2 * t2, t8 = t4 * t4;
            iAct = t8 < 1.0e-5 ? iReq * (1.0 - 0.125 * t8) : iReq / std::sqrt (std::sqrt (std::sqrt (1.0 + t8)));
        }
        double v = vt + (iAct - iReq) * P.load;
        // côté saturation : butée plus dure
        if (v < 0.0) v = AmpStage::rail (v, invSat);
        drive = std::max (iReq / iCut, -v * invSat);
        return xf.process (v);
    }

private:
    Params P;
    AmpStage poly;
    Transformer xf;
    double iCut = 0.045, invSat = 0.03, drive = 0.0;
};
} // namespace n73
