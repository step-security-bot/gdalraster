/* Implementation of class GDALVector
   Encapsulates a GDALDataset and one OGRLayer
   Chris Toney <chris.toney at usda.gov> */

#include "gdal.h"
#include "cpl_error.h"
#include "cpl_port.h"
#include "cpl_string.h"
#include "ogr_srs_api.h"

#include "gdalraster.h"
#include "gdalvector.h"


GDALVector::GDALVector() : 
				dsn_in(""),
				hDataset(nullptr),
				eAccess(GA_ReadOnly),
				hLayer(nullptr),
				bVirtual(true) {}

GDALVector::GDALVector(OGRLayerH lyr_obj) :
				dsn_in(""),
				hDataset(nullptr),
				eAccess(GA_ReadOnly),
				hLayer(lyr_obj),
				bVirtual(true) {}

GDALVector::GDALVector(Rcpp::CharacterVector dsn, std::string layer) : 
				GDALVector(dsn, layer, true) {}

GDALVector::GDALVector(Rcpp::CharacterVector dsn, std::string layer,
		bool read_only) :
				hDataset(nullptr),
				eAccess(GA_ReadOnly),
				hLayer(nullptr),
				bVirtual(false) {

	dsn_in = Rcpp::as<std::string>(_check_gdal_filename(dsn));
	if (!read_only)
		eAccess = GA_Update;

	unsigned int nOpenFlags = GDAL_OF_VECTOR | GDAL_OF_SHARED;
	if (read_only)
		nOpenFlags |= GDAL_OF_READONLY;
	else
		nOpenFlags |= GDAL_OF_UPDATE;
		
	hDataset = GDALOpenEx(dsn_in.c_str(), nOpenFlags, nullptr, nullptr, nullptr);
	if (hDataset == nullptr)
		Rcpp::stop("Open dataset failed.");
		
	hLayer = GDALDatasetGetLayerByName(hDataset, layer.c_str());
	if (hLayer == nullptr)
		Rcpp::stop("Failed to get layer object.");
	else
		OGR_L_ResetReading(hLayer);
		
}

GDALVector::GDALVector(Rcpp::CharacterVector dsn, std::string layer,
		bool read_only, Rcpp::CharacterVector open_options) :
				hDataset(nullptr),
				eAccess(GA_ReadOnly),
				hLayer(nullptr),
				bVirtual(false) {

	dsn_in = Rcpp::as<std::string>(_check_gdal_filename(dsn));
	if (!read_only)
		eAccess = GA_Update;

	std::vector<char *> dsoo(open_options.size() + 1);
	for (R_xlen_t i = 0; i < open_options.size(); ++i) {
		dsoo[i] = (char *) (open_options[i]);
	}
	dsoo.push_back(nullptr);

	unsigned int nOpenFlags = GDAL_OF_VECTOR | GDAL_OF_SHARED;
	if (read_only)
		nOpenFlags |= GDAL_OF_READONLY;
	else
		nOpenFlags |= GDAL_OF_UPDATE;

	hDataset = GDALOpenEx(dsn_in.c_str(), nOpenFlags, nullptr, dsoo.data(), nullptr);
	if (hDataset == nullptr)
		Rcpp::stop("Open raster failed.");
		
	hLayer = GDALDatasetGetLayerByName(hDataset, layer.c_str());
	if (hLayer == nullptr)
		Rcpp::stop("Failed to get layer object.");
	else
		OGR_L_ResetReading(hLayer);
		
}

std::string GDALVector::getDsn() const {
	return dsn_in;
}

bool GDALVector::isOpen() const {
	if (hDataset == nullptr)
		return false;
	else
		return true;
}

bool GDALVector::isVirtual() const {
	return bVirtual;
}

Rcpp::CharacterVector GDALVector::getFileList() const {
	_checkAccess(GA_ReadOnly);
	
	char **papszFiles;
	papszFiles = GDALGetFileList(hDataset);
	
	int items = CSLCount(papszFiles);
	if (items > 0) {
		Rcpp::CharacterVector files(items);
		for (int i=0; i < items; ++i) {
			files(i) = papszFiles[i];
		}
		CSLDestroy(papszFiles);
		return files;
	}
	else {
		CSLDestroy(papszFiles);
		return "";	
	}
}

std::string GDALVector::getDriverShortName() const {
	_checkAccess(GA_ReadOnly);
		
	GDALDriverH hDriver = GDALGetDatasetDriver(hDataset);
	return GDALGetDriverShortName(hDriver);
}

std::string GDALVector::getDriverLongName() const {
	_checkAccess(GA_ReadOnly);
		
	GDALDriverH hDriver = GDALGetDatasetDriver(hDataset);
	return GDALGetDriverLongName(hDriver);
}

Rcpp::List GDALVector::getLayerDefn() const {
	_checkAccess(GA_ReadOnly);
	
	OGRFeatureDefnH hFDefn = OGR_L_GetLayerDefn(hLayer);
	if (hFDefn == nullptr)
		Rcpp::stop("Error: could not obtain layer definition.");
	
	Rcpp::List list_out = Rcpp::List::create();
	int iField;

	for (iField=0; iField < OGR_FD_GetFieldCount(hFDefn); ++iField) {
		OGRFieldDefnH hFieldDefn = OGR_FD_GetFieldDefn(hFDefn, iField);
		if (hFieldDefn == nullptr)
			Rcpp::stop("Error: could not obtain field definition.");

		OGRFieldType fld_type = OGR_Fld_GetType(hFieldDefn);
		Rcpp::CharacterVector value(1);
		
		// TODO: add list types, date, time, binary, etc.
		if (fld_type == OFTInteger) {
			value[0] = "OFTInteger";
		}
		else if (fld_type == OFTInteger64) {
			value[0] = "OFTInteger64";
		}
		else if (fld_type == OFTReal) {
			value[0] = "OFTReal";
		}
		else if (fld_type == OFTString) {
			value[0] = "OFTString";
		}
		else {
			value[0] = "default (read as OFTString)";
		}
		list_out.push_back(value, OGR_Fld_GetNameRef(hFieldDefn));
	}

	int nGeomFieldCount = OGR_FD_GetGeomFieldCount(hFDefn);
	for (int i = 0; i < nGeomFieldCount; ++i) {
		// TODO: get geometry type
//		OGRGeometryH hGeometry = OGR_F_GetGeomFieldRef(hFeature, i);
//		if (hGeomFldDefn == nullptr)
//			Rcpp::stop("Error: could not obtain geometry field definition.");
//		char* pszWKT;
//		OGR_G_ExportToWkt(hGeometry, &pszWKT);
//		Rcpp::CharacterVector wkt(1);
//		wkt[0] = pszWKT;
		OGRGeomFieldDefnH hGeomFldDefn =
				OGR_FD_GetGeomFieldDefn(hFDefn, i);
		if (hGeomFldDefn == nullptr)
			Rcpp::stop("Error: could not obtain geometry field definition.");
		list_out.push_back("OGRwkbGeometryType", OGR_GFld_GetNameRef(hGeomFldDefn));
		// TODO: get possible spatial ref for this field OGR_GFld_GetSpatialRef()
		// where should spatial ref at the geometry field-level be stored
		//CPLFree(pszWKT);
	}
	
	return list_out;
}

void GDALVector::setAttributeFilter(std::string query) {
	_checkAccess(GA_ReadOnly);

	const char* query_in = NULL;
	if (query != "")
		query_in = query.c_str();
		
	if (OGR_L_SetAttributeFilter(hLayer, query_in) != OGRERR_NONE)
		Rcpp::stop("Error setting filter, possibly in the query expression");
}

double GDALVector::getFeatureCount(bool force) {
	// OGR_L_GetFeatureCount() returns GIntBig so we return as double to R.
	// GDAL doc: Note that some implementations of this method may alter the
	// read cursor of the layer.
	_checkAccess(GA_ReadOnly);
		
	return OGR_L_GetFeatureCount(hLayer, force);
}

SEXP GDALVector::getNextFeature() {
	_checkAccess(GA_ReadOnly);

	OGRFeatureH hFeature = OGR_L_GetNextFeature(hLayer);
	
	if (hFeature != nullptr) {
		Rcpp::List list_out = Rcpp::List::create();
		OGRFeatureDefnH hFDefn = OGR_L_GetLayerDefn(hLayer);
		if (hFDefn == nullptr)
			Rcpp::stop("Error: could not obtain layer definition.");
		int iField;

		for (iField=0; iField < OGR_FD_GetFieldCount(hFDefn); ++iField) {
			OGRFieldDefnH hFieldDefn = OGR_FD_GetFieldDefn(hFDefn, iField);
			if (hFieldDefn == nullptr)
				Rcpp::stop("Error: could not obtain field definition.");
			if (!OGR_F_IsFieldSet(hFeature, iField) ||
					OGR_F_IsFieldNull(hFeature, iField)) {
				continue;
			}

			// TODO: support date, time, binary, etc.
			OGRFieldType fld_type = OGR_Fld_GetType(hFieldDefn);
			if (fld_type == OFTInteger) {
				Rcpp::IntegerVector value(1);
				value[0] = OGR_F_GetFieldAsInteger(hFeature, iField);
				list_out.push_back(value, OGR_Fld_GetNameRef(hFieldDefn));
			}
			else if (fld_type == OFTInteger64) {
				// R does not have native int64 so handled as double for now
				Rcpp::NumericVector value(1);
				value[0] = static_cast<double>(
						OGR_F_GetFieldAsInteger64(hFeature, iField));
				list_out.push_back(value, OGR_Fld_GetNameRef(hFieldDefn));
			}
			else if (fld_type == OFTReal) {
				Rcpp::NumericVector value(1);
				value[0] = OGR_F_GetFieldAsDouble(hFeature, iField);
				list_out.push_back(value, OGR_Fld_GetNameRef(hFieldDefn));
			}
			else {
				// read as string for now
				Rcpp::CharacterVector value(1);
				value[0] = OGR_F_GetFieldAsString(hFeature, iField);
				list_out.push_back(value, OGR_Fld_GetNameRef(hFieldDefn));
			}
		}

		int nGeomFieldCount = OGR_F_GetGeomFieldCount(hFeature);
		for (int i = 0; i < nGeomFieldCount; ++i) {
			OGRGeometryH hGeometry = OGR_F_GetGeomFieldRef(hFeature, i);
			if (hGeometry == nullptr)
				Rcpp::stop("Error: could not obtain geometry reference.");
			char* pszWKT;
			OGR_G_ExportToWkt(hGeometry, &pszWKT);
			Rcpp::CharacterVector wkt(1);
			wkt[0] = pszWKT;
			OGRGeomFieldDefnH hGeomFldDefn =
					OGR_F_GetGeomFieldDefnRef(hFeature, i);
			if (hGeomFldDefn == nullptr)
				Rcpp::stop("Error: could not obtain geometry field definition.");
			list_out.push_back(wkt, OGR_GFld_GetNameRef(hGeomFldDefn));
			CPLFree(pszWKT);
		}

		return list_out;
	}
	else {
		return R_NilValue;
	}
}

void GDALVector::resetReading() {
	_checkAccess(GA_ReadOnly);
	
	OGR_L_ResetReading(hLayer);
}

void GDALVector::close() {
	GDALReleaseDataset(hDataset);
	hDataset = nullptr;
}

// ****************************************************************************
// class methods for internal use not exposed in R
// ****************************************************************************

void GDALVector::_checkAccess(GDALAccess access_needed) const {
	if (!isOpen())
		Rcpp::stop("Raster dataset is not open.");
	
	if (access_needed == GA_Update && eAccess == GA_ReadOnly)
		Rcpp::stop("Dataset is read-only.");
}


// ****************************************************************************

RCPP_MODULE(mod_GDALVector) {

    Rcpp::class_<GDALVector>("GDALVector")

    .constructor
    	("Default constructor, only for allocation in std::vector.")
    .constructor<OGRLayerH>
    	("Usage: new(GDALVector, lyr_obj)")
    .constructor<Rcpp::CharacterVector, std::string>
    	("Usage: new(GDALVector, dsn, layer)")
    .constructor<Rcpp::CharacterVector, std::string, bool>
    	("Usage: new(GDALVector, dsn, layer, read_only=[TRUE|FALSE])")
    .constructor<Rcpp::CharacterVector, std::string, bool, Rcpp::CharacterVector>
    	("Usage: new(GDALVector, dsn, layer, read_only, open_options)")
    
    // exposed member functions
    .const_method("getDsn", &GDALVector::getDsn,
    	"Return the DSN.")
    .const_method("isOpen", &GDALVector::isOpen,
    	"Is the dataset open?")
    .const_method("isVirtual", &GDALVector::isVirtual,
    	"Is this a virtual layer?")
    .const_method("getFileList", &GDALVector::getFileList,
    	"Fetch files forming dataset.")
    .const_method("getDriverShortName", &GDALVector::getDriverShortName,
    	 "Return the short name of the format driver.")
    .const_method("getDriverLongName", &GDALVector::getDriverLongName,
    	"Return the long name of the format driver.")
    .const_method("getLayerDefn", &GDALVector::getLayerDefn,
    	"Fetch the schema information for this layer.")
    .method("setAttributeFilter", &GDALVector::setAttributeFilter,
    	"Set a new attribute query.")
    .method("getFeatureCount", &GDALVector::getFeatureCount,
    	"Fetch the feature count in this layer.")
    .method("getNextFeature", &GDALVector::getNextFeature,
    	"Fetch the next available feature from this layer.")
    .method("resetReading", &GDALVector::resetReading,
    	"Reset feature reading to start on the first feature.")
    .method("close", &GDALVector::close,
    	"Release the dataset for proper cleanup.")
    
    ;
}
