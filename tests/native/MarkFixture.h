#pragma once
#include <QByteArray>

// Nested and mirrored mark, with four solid bulged sectors as in production DXF.
inline QByteArray markFixture() {
    QByteArray hatch="0\nHATCH\n8\nMARK\n70\n1\n91\n4\n";
    for(const auto& loop:{
        "10\n0\n20\n-0.5\n42\n0\n10\n0\n20\n0\n42\n0\n10\n-0.5\n20\n0\n42\n0.4142135623730951\n",
        "10\n0.5\n20\n0\n42\n0\n10\n0\n20\n0\n42\n0\n10\n0\n20\n-0.5\n42\n0.4142135623730951\n",
        "10\n0\n20\n0\n42\n0\n10\n0\n20\n0.5\n42\n0.4142135623730951\n10\n-0.5\n20\n0\n42\n0\n",
        "10\n0.5\n20\n0\n42\n0.4142135623730951\n10\n0\n20\n0.5\n42\n0\n10\n0\n20\n0\n42\n0\n"})
        hatch+=QByteArray("92\n7\n72\n1\n73\n1\n93\n3\n")+loop+"97\n0\n";
    hatch+="75\n1\n76\n1\n98\n0\n";
    return QByteArray("0\nSECTION\n2\nBLOCKS\n0\nBLOCK\n2\nMARK\n10\n0\n20\n0\n")+
        "0\nCIRCLE\n8\nMARK\n10\n0\n20\n0\n40\n1\n"+hatch+
        "0\nENDBLK\n0\nBLOCK\n2\nPANEL\n10\n0\n20\n0\n"
        "0\nINSERT\n2\nMARK\n10\n2\n20\n3\n"
        "0\nINSERT\n2\nMARK\n10\n2\n20\n-4\n"
        "0\nENDBLK\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"
        "0\nINSERT\n2\nPANEL\n10\n20\n20\n30\n41\n-1\n42\n1\n"
        "0\nENDSEC\n0\nEOF\n";
}
