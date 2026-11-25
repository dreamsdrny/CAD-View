#pragma once
#include <QByteArray>

inline QByteArray textDimensionFixture() {
    return "0\nSECTION\n2\nBLOCKS\n0\nBLOCK\n2\n*D1\n10\n0\n20\n0\n"
        "0\nLINE\n8\n0\n10\n10\n20\n20\n11\n30\n21\n20\n"
        "0\nTEXT\n8\n0\n10\n0\n20\n0\n11\n20\n21\n21\n40\n2\n72\n1\n73\n2\n1\n20\n"
        "0\nENDBLK\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"
        "0\nMTEXT\n10\n50\n20\n60\n40\n2\n71\n1\n3\n{\\C1;First\\P\n1\nSecond} \\S1#2; \\U+4E2D\n"
        "0\nTEXT\n10\n0\n20\n0\n11\n40\n21\n50\n40\n2\n41\n0.8\n72\n2\n73\n3\n1\nRight\n"
        "0\nDIMENSION\n2\n*D1\n8\nDIMS\n10\n100\n20\n200\n70\n32\n"
        "0\nENDSEC\n0\nEOF\n";
}

inline QByteArray editableDimensionFixture() {
    return "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1024\n0\nENDSEC\n"
        "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nBLOCK_RECORD\n5\nA\n70\n1\n"
        "0\nBLOCK_RECORD\n5\nB\n330\nA\n100\nAcDbSymbolTableRecord\n100\nAcDbBlockTableRecord\n2\n*D1\n0\nENDTAB\n0\nENDSEC\n"
        "0\nSECTION\n2\nBLOCKS\n0\nBLOCK\n5\nC\n330\nB\n2\n*D1\n10\n0\n20\n0\n"
        "0\nLINE\n5\nD\n330\nB\n8\n0\n10\n0\n20\n10\n11\n120\n21\n10\n"
        "0\nMTEXT\n5\nE\n330\nB\n8\n0\n10\n60\n20\n11\n40\n3\n71\n5\n1\n\\A1;120.000%%P0.2\n7\nStandard\n"
        "0\nPOINT\n5\nF\n330\nB\n8\nDefpoints\n10\n0\n20\n0\n"
        "0\nENDBLK\n5\n10\n330\nB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"
        "0\nDIMENSION\n5\n11\n2\n*D1\n8\nDIMS\n10\n120\n20\n10\n70\n32\n1\n<>%%P0.2\n42\n120\n3\nISO-25\n1001\nACAD\n1000\nKEEP-DSTYLE\n"
        "0\nDIMENSION\n5\n12\n2\n*D1\n8\nDIMS\n10\n120\n20\n10\n70\n32\n1\n<>%%P0.2\n42\n120\n3\nISO-25\n"
        "0\nENDSEC\n0\nEOF\n";
}
