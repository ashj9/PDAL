/******************************************************************************
* Copyright (c) 2011, Michael P. Gerlek (mpg@flaxen.com)
*
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following
* conditions are met:
*
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in
*       the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of Hobu, Inc. or Flaxen Geo Consulting nor the
*       names of its contributors may be used to endorse or promote
*       products derived from this software without specific prior
*       written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
* FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
* COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
* OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
* AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
* OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
* OF SUCH DAMAGE.
****************************************************************************/

/**
#pragma warning(disable: 4127)  // conditional expression is constant
**/

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_api.h>
#include <ogr_geometry.h>
#include <ogrsf_frmts.h>

#include <nlohmann/json.hpp>
#include <pdal/Polygon.hpp>
#include <pdal/private/SrsTransform.hpp>
#include <pdal/private/gdal/SpatialRef.hpp>

#include "GDALUtils.hpp"

namespace pdal
{

namespace oldgdalsupport
{
	OGRErr createFromWkt(char **s, OGRGeometry **newGeom);
	OGRGeometry* createFromGeoJson(char **s);
} // namespace oldgdalsupport


namespace gdal
{

/**
  Reproject a point from a source projection to a destination.
  \param x  X coordinate of point to be reprojected in-place.
  \param y  Y coordinate of point to be reprojected in-place.
  \param z  Z coordinate of point to be reprojected in-place.
  \param srcSrs  Source SRS
  \param dstSrs  Destination SRS
  \return  Whether the reprojection was successful or not.
*/
bool reproject(double& x, double& y, double& z, const SpatialReference& srcSrs,
    const SpatialReference& dstSrs)
{
    return SrsTransform(srcSrs, dstSrs).transform(x, y, z);
}


/**
  Reproject a bounds box from a source projection to a destination.
  \param box  Bounds box to be reprojected in-place.
  \param srcSrs  Source SRS.
  \param dstSrs  Destination SRS.
  \return  Whether the reprojection was successful or not.
*/
bool reprojectBounds(BOX3D& box, const SpatialReference& srcSrs,
    const SpatialReference& dstSrs)
{
    SrsTransform transform(srcSrs, dstSrs);

    bool ok = transform.transform(box.minx, box.miny, box.minz);
    if (ok)
        ok = transform.transform(box.maxx, box.maxy, box.maxz);
    return ok;
}


/**
  Reproject a bounds box from a source projection to a destination.
  \param box  2D or 3D bounds box to be reprojected.
  \param srcSrs  Source SRS.
  \param dstSrs  Destination SRS.
  \return  Whether the reprojection was successful or not.
*/
bool reprojectBounds(Bounds& box, const SpatialReference& srcSrs,
    const SpatialReference& dstSrs)
{
    bool ok = false;
    if (box.is3d())
    {
        BOX3D b3 = box.to3d();
        ok = reprojectBounds(b3, srcSrs, dstSrs);
        box.reset(b3);
    }
    else
    {
        BOX2D b2 = box.to2d();
        ok = reprojectBounds(b2, srcSrs, dstSrs);
        box.reset(b2);
    }
    return ok;
}

/**
  Reproject a bounds box from a source projection to a destination.
  \param box  2D Bounds box to be reprojected in-place.
  \param srcSrs  Source SRS.
  \param dstSrs  Destination SRS.
  \return  Whether the reprojection was successful or not.
*/
bool reprojectBounds(BOX2D& box, const SpatialReference& srcSrs,
    const SpatialReference& dstSrs)
{
    BOX3D b(box);
    bool res = reprojectBounds(b, srcSrs, dstSrs);
    box = b.to2d();
    return res;
}


/**
  Get the last error from a GDAL/OGR operation as a string.
  \return  Error message.
*/
std::string lastError()
{
    return CPLGetLastErrorMsg();
}


/**
  Register GDAL/OGR drivers.
*/
void registerDrivers()
{
    static std::once_flag flag;

    auto init = []() -> void
    {
        GDALAllRegister();
        OGRRegisterAll();
    };

    std::call_once(flag, init);
}


/**
  Unregister GDAL/OGR drivers.
*/
void unregisterDrivers()
{
    GDALDestroyDriverManager();
}


/**
  Create OGR geometry given a well-known text string.
  \param s  WKT string to convert to OGR Geometry.
  \return  Pointer to new geometry.
*/
OGRGeometry *createFromWkt(const char *s)
{
    OGRGeometry *newGeom;
#if ((GDAL_VERSION_MAJOR == 2) && GDAL_VERSION_MINOR < 3)
    char *cs = const_cast<char *>(s);
    oldgdalsupport::createFromWkt(&cs, &newGeom);
#else
    OGRGeometryFactory::createFromWkt(s, nullptr, &newGeom);
#endif
    return newGeom;
}


/**
  Create OGR geometry given a well-known text string and text SRS.
  \param s  WKT string to convert to OGR Geometry.
  \param srs  Text representation of coordinate reference system.
  \return  Pointer to new geometry.
*/
OGRGeometry *createFromWkt(const std::string& s, std::string& srs)
{
    OGRGeometry *newGeom;
	char *buf = const_cast<char *>(s.data());
#if ((GDAL_VERSION_MAJOR == 2) && GDAL_VERSION_MINOR < 3)
    oldgdalsupport::createFromWkt(&buf, &newGeom);
#else
    OGRGeometryFactory::createFromWkt(&buf, nullptr, &newGeom);
    if (!newGeom)
        throw pdal_error("Couldn't convert WKT string to geometry.");
    srs = buf;
#endif

	std::string::size_type pos = 0;
	pos = Utils::extractSpaces(srs, pos);
	if (pos == srs.size())
		srs.clear();
    else
    {
        if (srs[pos++] != '/')
            throw pdal_error("Invalid character following valid geometry.");
        pos += Utils::extractSpaces(srs, pos);
        srs = srs.substr(pos);
    }

    return newGeom;
}


/**
  Create OGR geometry given a GEOjson text string.
  \param s  GEOjson string to convert to OGR Geometry.
  \return  Pointer to new geometry.
*/
OGRGeometry *createFromGeoJson(const char *s)
{
#if ((GDAL_VERSION_MAJOR == 2) && GDAL_VERSION_MINOR < 3)
    char* p = const_cast<char*>(s);
    return oldgdalsupport::createFromGeoJson((char**)&p);
#else
    return OGRGeometryFactory::createFromGeoJson(s);
#endif
}


/**
  Create OGR geometry given a GEOjson text string and text SRS.
  \param s  GEOjson string to convert to OGR Geometry.
  \param srs  Text representation of coordinate reference system.
  \return  Pointer to new geometry.
*/
OGRGeometry *createFromGeoJson(const std::string& s, std::string& srs)
{
// Call this function instead after we've past supporting GDAL 2.2
//    return OGRGeometryFactory::createFromGeoJson(s);

    char *cs = const_cast<char *>(s.data());
    OGRGeometry *newGeom = oldgdalsupport::createFromGeoJson(&cs);
    if (!newGeom)
        throw pdal_error("Couldn't convert GeoJSON to geometry.");
    srs = cs;

	std::string::size_type pos = 0;
	pos = Utils::extractSpaces(srs, pos);
	if (pos == srs.size())
		srs.clear();
    else
    {
        if (srs[pos++] != '/')
            throw pdal_error("Invalid character following valid geometry.");
        pos += Utils::extractSpaces(srs, pos);
        srs = srs.substr(pos);
    }
    return newGeom;
}


/**
  Load polygons from an OGR datasource specified by JSON.
  \param ogr  JSON that specifies how to load data.
  \return  Vector of polygons read from datasource.
*/
std::vector<Polygon> getPolygons(const NL::json& ogr)
{
    registerDrivers();
    const NL::json& datasource = ogr.at("datasource");

    char** papszDriverOptions = nullptr;
    if (ogr.count("drivers"))
    {

        const NL::json& dops = ogr.at("drivers");
        std::vector<std::string> driverOptions =
            dops.get<std::vector<std::string>>();
        for (const auto& s: driverOptions)
            papszDriverOptions = CSLAddString(papszDriverOptions, s.c_str());
    }
    std::vector<const char*> openoptions{};

    char** papszOpenOptions = nullptr;
    if (ogr.count("openoptions"))
    {
        const NL::json& oops = ogr.at("openoptions");
        std::vector<std::string> openOptions =
            oops.get<std::vector<std::string>>();
        for(const auto& s: openOptions)
            papszOpenOptions = CSLAddString(papszOpenOptions, s.c_str());
    }

    std::string dsString = datasource.get<std::string>();
    unsigned int openFlags =
        GDAL_OF_READONLY | GDAL_OF_VECTOR | GDAL_OF_VERBOSE_ERROR;
    GDALDataset* ds;
    ds = (GDALDataset*) GDALOpenEx(dsString.c_str(), openFlags,
        papszDriverOptions, papszOpenOptions, NULL);
    CSLDestroy(papszDriverOptions);
    CSLDestroy(papszOpenOptions);
    if (!ds)
        throw pdal_error("Unable to read OGR datasource: " + datasource.dump());

    OGRLayer* poLayer(nullptr);
    if (ogr.count("layer"))
    {
        const NL::json& layer = ogr.at("layer");
        std::string lyrString = layer.get<std::string>();
        poLayer = ds->GetLayerByName( lyrString.c_str() );

        if (!poLayer)
            throw pdal_error("Unable to read OGR layer: " + layer.dump());
    }

    OGRFeature *poFeature (nullptr);
    if (ogr.count("sql"))
    {
        std::string dialect("OGRSQL");
        std::string query = ogr.at("sql").get<std::string>();

        Polygon poly;
        OGRGeometry *geom = nullptr;
        if (ogr.count("options"))
        {
            const NL::json options = ogr.at("options");
            if (options.count("dialect"))
                dialect = options.at("dialect").get<std::string>();

            if (options.count("geometry"))
            {
                // Determine the layer's SRS and assign it to the geometry
                // or transform to that SRS.
                poLayer = ds->ExecuteSQL(query.c_str(), NULL, dialect.c_str());
                if (!poLayer)
                    throw pdal_error("Unable to execute OGR SQL query.");

                SpatialRef sref;
                sref.setFromLayer(poLayer);
                ds->ReleaseResultSet(poLayer);

                poly.update(options.at("geometry").get<std::string>());
                if (poly.getSpatialReference().valid())
                {
                    auto ok = poly.transform(sref.wkt());
                    if (!ok)
                        throw pdal_error(ok.what());
                }
                else
                    poly.setSpatialReference(sref.wkt());

                geom = (OGRGeometry *)poly.getOGRHandle();
            }
        }
        poLayer = ds->ExecuteSQL(query.c_str(), geom, dialect.c_str());
        if (!poLayer)
            throw pdal_error("unable to execute sql query!");
    }

    std::vector<Polygon> polys;
    while ((poFeature = poLayer->GetNextFeature()) != NULL)
    {
        polys.emplace_back(poFeature->GetGeometryRef());
        OGRFeature::DestroyFeature( poFeature );
    }
    ds->ReleaseResultSet(poLayer);
    return polys;
}

} // namespace gdal

namespace oldgdalsupport
{

/**
  Create an OGRGeometry from a WKT string.
  
  \param s Pointer to an array of characters to parse as WKT.
  \param ppoReturn  Address in which to place a pointer to a created
    OGRGeometry.
*/
#if (GDAL_VERSION_MAJOR == 2) && (GDAL_VERSION_MINOR < 3)
OGRErr createFromWkt(char **s, OGRGeometry **ppoReturn )
{
    const char *pszInput = *s;
    *ppoReturn = nullptr;

/* -------------------------------------------------------------------- */
/*      Get the first token, which should be the geometry type.         */
/* -------------------------------------------------------------------- */
    char szToken[1000] = {};
    if( OGRWktReadToken( pszInput, szToken ) == nullptr )
        return OGRERR_CORRUPT_DATA;

/* -------------------------------------------------------------------- */
/*      Instantiate a geometry of the appropriate type.                 */
/* -------------------------------------------------------------------- */
    OGRGeometry *poGeom = nullptr;
    if( STARTS_WITH_CI(szToken, "POINT") )
    {
        poGeom = new OGRPoint();
    }
    else if( STARTS_WITH_CI(szToken, "LINESTRING") )
    {
        poGeom = new OGRLineString();
    }
    else if( STARTS_WITH_CI(szToken, "POLYGON") )
    {
        poGeom = new OGRPolygon();
    }
    else if( STARTS_WITH_CI(szToken,"TRIANGLE") )
    {
        poGeom = new OGRTriangle();
    }
    else if( STARTS_WITH_CI(szToken, "GEOMETRYCOLLECTION") )
    {
        poGeom = new OGRGeometryCollection();
    }
    else if( STARTS_WITH_CI(szToken, "MULTIPOLYGON") )
    {
        poGeom = new OGRMultiPolygon();
    }
    else if( STARTS_WITH_CI(szToken, "MULTIPOINT") )
    {
        poGeom = new OGRMultiPoint();
    }
    else if( STARTS_WITH_CI(szToken, "MULTILINESTRING") )
    {
        poGeom = new OGRMultiLineString();
    }
    else if( STARTS_WITH_CI(szToken, "CIRCULARSTRING") )
    {
        poGeom = new OGRCircularString();
    }
    else if( STARTS_WITH_CI(szToken, "COMPOUNDCURVE") )
    {
        poGeom = new OGRCompoundCurve();
    }
    else if( STARTS_WITH_CI(szToken, "CURVEPOLYGON") )
    {
        poGeom = new OGRCurvePolygon();
    }
    else if( STARTS_WITH_CI(szToken, "MULTICURVE") )
    {
        poGeom = new OGRMultiCurve();
    }
    else if( STARTS_WITH_CI(szToken, "MULTISURFACE") )
    {
        poGeom = new OGRMultiSurface();
    }

    else if( STARTS_WITH_CI(szToken,"POLYHEDRALSURFACE") )
    {
        poGeom = new OGRPolyhedralSurface();
    }

    else if( STARTS_WITH_CI(szToken,"TIN") )
    {
        poGeom = new OGRTriangulatedSurface();
    }

    else
    {
        return OGRERR_UNSUPPORTED_GEOMETRY_TYPE;
    }

/* -------------------------------------------------------------------- */
/*      Do the import.                                                  */
/* -------------------------------------------------------------------- */
    const OGRErr eErr = poGeom->importFromWkt(s);

/* -------------------------------------------------------------------- */
/*      Assign spatial reference system.                                */
/* -------------------------------------------------------------------- */
    if( eErr == OGRERR_NONE )
    {
        if( poGeom->hasCurveGeometry() &&
            CPLTestBool(CPLGetConfigOption("OGR_STROKE_CURVE", "FALSE")) )
        {
            OGRGeometry* poNewGeom = poGeom->getLinearGeometry();
            delete poGeom;
            poGeom = poNewGeom;
        }
        *ppoReturn = poGeom;
    }
    else
    {
        delete poGeom;
    }

    return eErr;
}
#endif // GDAL version limit


/**
  Create an OGRGeometry given a GEOjson string.
  \param s  Pointer to GEOjson string to be parsed.  The string is updated
    such that s points just past the extracted JSON.
  \return  Pointer to created OGRGeometry.
*/
OGRGeometry *createFromGeoJson(char **s)
{
    // Go through a supposed JSON object string, looking for the
    // closing brace.  Return just past its position.
    auto findEnd = [](std::string s, std::string::size_type pos)
    {
        bool inString(false);
        std::string check("{}\"");
        std::string::size_type startPos(pos);
        pos = Utils::extractSpaces(s, pos);
        if (s[pos++] != '{')
            return std::string::npos;
        int cnt = 1;
        while (cnt && pos != std::string::npos)
        {
            pos = s.find_first_of(check, pos);
            if (pos == std::string::npos)
                return pos;
            if (s[pos] == '"')
            {
                // We're guaranteed that the beginning seq. of chars is such
                // we won't check an invalid ref.
                if (!inString || s[pos - 1] != '\\' || s[pos - 2] == '\\')
                    inString = !inString;
            }
            else if (!inString && s[pos] == '{')
                cnt++;
            else if (!inString && s[pos] == '}')
                cnt--;
            pos++;
        }
        if (cnt != 0)
            return std::string::npos;
        return pos;
    };

    std::string ss(*s);
    // Search the string for the end of the JSON.
    std::string::size_type pos = findEnd(ss, 0);
    if (pos == std::string::npos)
        return nullptr;

    // Just send the JSON stuff to the OGR function.
    ss = ss.substr(0, pos);
    OGRGeometryH h = OGR_G_CreateGeometryFromJson(ss.c_str());

    // Increment the initial string pointer to just past the JSON.
    *s += pos;
    return (reinterpret_cast<OGRGeometry *>(h));
}

} // namespace oldgdalsupport

} // namespace pdal
