/*
	Copyright 2017 TeamWin
	This file is part of TWRP/TeamWin Recovery Project.

	TWRP is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	TWRP is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with TWRP.  If not, see <http://www.gnu.org/licenses/>.
*/

// resource.cpp - Source to manage GUI resources

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <fcntl.h>

#include "../zipwrap.hpp"
extern "C" {
#include "../twcommon.h"
#include "gui.h"
#include <aroma.h>
}
#include "../minuitwrp/minui.h"

#include "rapidxml.hpp"
#include "objects.hpp"

#define TMP_RESOURCE_NAME   "/tmp/extract.bin"

Resource::Resource(xml_node<>* node, ZipWrap* pZip __unused)
{
	if (node && node->first_attribute("name"))
		mName = node->first_attribute("name")->value();
}

int Resource::ExtractResource(ZipWrap* pZip, std::string folderName, std::string fileName, std::string fileExtn, std::string destFile)
{
	if (!pZip)
		return -1;

	std::string src = folderName + "/" + fileName + fileExtn;
	if (!pZip->ExtractEntry(src, destFile, 0666))
		return -1;
	return 0;
}

void Resource::LoadImage(ZipWrap* pZip, std::string file, gr_surface* surface)
{
	int rc = 0;
	if (ExtractResource(pZip, "images", file, ".png", TMP_RESOURCE_NAME) == 0)
	{
		rc = res_create_surface(TMP_RESOURCE_NAME, surface);
		unlink(TMP_RESOURCE_NAME);
	}
	else if (ExtractResource(pZip, "images", file, "", TMP_RESOURCE_NAME) == 0)
	{
		// JPG includes the .jpg extension in the filename so extension should be blank
		rc = res_create_surface(TMP_RESOURCE_NAME, surface);
		unlink(TMP_RESOURCE_NAME);
	}
	else if (!pZip)
	{
		// File name in xml may have included .png so try without adding .png
		rc = res_create_surface(file.c_str(), surface);
	}
	if (rc != 0)
		LOGINFO("Failed to load image from %s%s, error %d\n", file.c_str(), pZip ? " (zip)" : "", rc);
}

void Resource::LoadCanvas(ZipWrap* pZip, std::string file, LIBAROMA_CANVASP *canvas)
{
	int rc = 0;
	LIBAROMA_CANVASP loaded_cv = NULL;
	LOGINFO("LoadCanvas file=\"%s\", TMP=\"%s\"\n", file.c_str(), TMP_RESOURCE_NAME);
	if (ExtractResource(pZip, "images", file, ".png", TMP_RESOURCE_NAME) == 0)
	{
		LOGINFO("Using file load method #1\n");
		//rc = res_create_surface(TMP_RESOURCE_NAME, surface);
		loaded_cv = libaroma_image_file((char *)TMP_RESOURCE_NAME, 0);
		if (!loaded_cv) {
			LOGERR("Failed to load canvas from %s\n", TMP_RESOURCE_NAME);
			rc = -1;
		}
		unlink(TMP_RESOURCE_NAME);
	}
	else if (ExtractResource(pZip, "images", file, "", TMP_RESOURCE_NAME) == 0)
	{
		LOGINFO("Using file load method #2\n");
		// JPG includes the .jpg extension in the filename so extension should be blank
		//rc = res_create_surface(TMP_RESOURCE_NAME, surface);
		loaded_cv = libaroma_image_file((char *)TMP_RESOURCE_NAME, 0);
		if (!loaded_cv) {
			LOGERR("Failed to load canvas from %s\n", TMP_RESOURCE_NAME);
			rc = -1;
		}
		unlink(TMP_RESOURCE_NAME);
	}
	else if (!pZip)
	{
		LOGINFO("Using file load method #3\n");
		// File name in xml may have included .png so try without adding .png
		//rc = res_create_surface(file.c_str(), surface);
		char c_path[256]={0};
		snprintf(c_path, 256, TWRES "/images/%s.png", file.c_str());
		loaded_cv = libaroma_image_file(c_path, 0);
		if (!loaded_cv) {
			LOGERR("Failed to load canvas from %s\n", c_path);
			rc = -1;
		}
	}
	if (rc != 0){
		LOGINFO("Failed to load image from %s%s, error %d\n", file.c_str(), pZip ? " (zip)" : "", rc);
	}
	else {
		LOGINFO("Setting canvas pointer %p to %p\n", canvas, loaded_cv);
		*canvas = loaded_cv;
		LOGINFO("Canvas set to %p\n", *canvas);
	}
}

void Resource::CheckAndScaleImage(gr_surface source, gr_surface* destination, int retain_aspect)
{
	if (!source) {
		*destination = NULL;
		return;
	}
	if (get_scale_w() != 0 && get_scale_h() != 0) {
		float scale_w = get_scale_w(), scale_h = get_scale_h();
		if (retain_aspect) {
			if (scale_w < scale_h)
				scale_h = scale_w;
			else
				scale_w = scale_h;
		}
		if (res_scale_surface(source, destination, scale_w, scale_h)) {
			LOGINFO("Error scaling image, using regular size.\n");
			*destination = source;
		}
	} else {
		*destination = source;
	}
}

void Resource::CheckAndScaleCanvas(LIBAROMA_CANVASP source, LIBAROMA_CANVASP *destination, int retain_aspect)
{
	if (!source) {
		*destination = NULL;
		return;
	}
	if (get_scale_w() != 0 && get_scale_h() != 0) {
		float scale_w = get_scale_w(), scale_h = get_scale_h();
		if (retain_aspect) {
			if (scale_w < scale_h)
				scale_h = scale_w;
			else
				scale_w = scale_h;
		}
		byte scaled = 0;
		LOGINFO("scaling (%dx%d) -> (%dx%d)\n", source->w, source->h, source->w*scale_w, source->h*scale_h);
		LIBAROMA_CANVASP temp_cv = libaroma_canvas_alpha(source->w*scale_w, source->h*scale_h);
		if (!temp_cv){
			LOGERR("Error allocating memory for scaled image, using regular size.\n");
			*destination = source;
			return;
		}
		libaroma_canvas_fillalpha(temp_cv, 0, 0, temp_cv->w, temp_cv->h, 0);
		LOGINFO("Scaling canvas with size (%dx%d)\n", temp_cv->w, temp_cv->h);
		scaled = libaroma_draw_scale_smooth(temp_cv, source, 0, 0, temp_cv->w, temp_cv->h, 0, 0, source->w, source->h);
		if (!scaled) {
			LOGINFO("Error scaling image, using regular size.\n");
			libaroma_canvas_free(temp_cv);
			*destination = source;
		}
		else {
			LOGINFO("Canvas need scaling, setting to %p\n", temp_cv);
			*destination = temp_cv;
		}
	} else {
		LOGINFO("Canvas not need scaling, setting to %p\n", source);
		*destination = source;
	}
}

FontResource::FontResource(xml_node<>* node, ZipWrap* pZip)
 : Resource(node, pZip)
{
	origFontSize = 0;
	origFont = NULL;
	LoadFont(node, pZip);
}

void FontResource::LoadFont(xml_node<>* node, ZipWrap* pZip)
{
	std::string file;
	xml_attribute<>* attr;

	mFont = NULL;
	if (!node)
		return;

	attr = node->first_attribute("filename");
	if (!attr)
		return;

	file = attr->value();
	LOGINFO("Loading font at %s\n", file.c_str());

	if (file.size() >= 4 && file.compare(file.size()-4, 4, ".ttf") == 0)
	{
		int font_size = 0;

		if (origFontSize != 0) {
			attr = node->first_attribute("scale");
			if (attr == NULL)
				return;
			font_size = origFontSize * atoi(attr->value()) / 100;
		} else {
			attr = node->first_attribute("size");
			if (attr == NULL)
				return;
			font_size = scale_theme_min(atoi(attr->value()));
			origFontSize = font_size;
		}

		int dpi = 300;

		attr = node->first_attribute("dpi");
		if (attr)
			dpi = atoi(attr->value());

		// we can't use TMP_RESOURCE_NAME here because the ttf subsystem is caching the name and scaling needs to reload the font
		std::string tmpname = "/tmp/" + file;
		if (ExtractResource(pZip, "fonts", file, "", tmpname) == 0)
		{
			LOGINFO("Font name (#1) now is %s\n", tmpname.c_str());
			libaroma_font(0, libaroma_stream_file(((char*)tmpname.c_str())));
			//mFont = gr_ttf_loadFont(tmpname.c_str(), font_size, dpi);
		}
		else
		{
			file = std::string(TWRES "fonts/") + file;
			LOGINFO("Font name (#2) now is %s\n", file.c_str());
			//mFont = gr_ttf_loadFont(file.c_str(), font_size, dpi);
			libaroma_font(0, libaroma_stream_file(((char*)file.c_str())));
		}
	}
	else
	{
		LOGERR("Non-TTF fonts are no longer supported.\n");
	}
}

void FontResource::DeleteFont() {
	if (mFont)
		gr_ttf_freeFont(mFont);
	mFont = NULL;
	if (origFont)
		gr_ttf_freeFont(origFont);
	origFont = NULL;
}

void FontResource::Override(xml_node<>* node, ZipWrap* pZip) {
	if (!origFont) {
		origFont = mFont;
	} else if (mFont) {
		gr_ttf_freeFont(mFont);
		mFont = NULL;
	}
	LoadFont(node, pZip);
}

FontResource::~FontResource()
{
	DeleteFont();
}

ImageResource::ImageResource(xml_node<>* node, ZipWrap* pZip)
 : Resource(node, pZip)
{
	std::string file;
	gr_surface temp_surface = NULL;
	LIBAROMA_CANVASP temp_canvas = NULL;

	mSurface = NULL;
	if (!node) {
		LOGERR("ImageResource node is NULL\n");
		return;
	}

	if (node->first_attribute("filename"))
		file = node->first_attribute("filename")->value();
	else {
		LOGERR("No filename specified for image resource.\n");
		return;
	}

	bool retain_aspect = (node->first_attribute("retainaspect") != NULL);
	// the value does not matter, if retainaspect is present, we assume that we want to retain it
	LoadImage(pZip, file, &temp_surface);
	CheckAndScaleImage(temp_surface, &mSurface, retain_aspect);
	LOGINFO("Temp Canvas pointer = %p\n", &temp_canvas);
	LoadCanvas(pZip, file, &temp_canvas);
	CheckAndScaleCanvas(temp_canvas, &mCanvas, retain_aspect);
}

ImageResource::~ImageResource()
{
	if (mSurface)
		res_free_surface(mSurface);
	if (mCanvas)
		libaroma_canvas_free(mCanvas);
}

AnimationResource::AnimationResource(xml_node<>* node, ZipWrap* pZip)
 : Resource(node, pZip)
{
	std::string file;
	int fileNum = 1;

	if (!node)
		return;

	if (node->first_attribute("filename"))
		file = node->first_attribute("filename")->value();
	else {
		LOGERR("No filename specified for image resource.\n");
		return;
	}

	bool retain_aspect = (node->first_attribute("retainaspect") != NULL);
	// the value does not matter, if retainaspect is present, we assume that we want to retain it
	for (;;)
	{
		std::ostringstream fileName;
		fileName << file << std::setfill ('0') << std::setw (3) << fileNum;

		gr_surface surface, temp_surface = NULL;
		LIBAROMA_CANVASP canvas, temp_canvas = NULL;
		LoadImage(pZip, fileName.str(), &temp_surface);
		LoadCanvas(pZip, fileName.str(), &temp_canvas);
		CheckAndScaleImage(temp_surface, &surface, retain_aspect);
		CheckAndScaleCanvas(temp_canvas, &canvas, retain_aspect);
		if (surface && canvas) {
			mSurfaces.push_back(surface);
			mCanvases.push_back(canvas);
			fileNum++;
		} else
			break; // Done loading animation images
	}
}

AnimationResource::~AnimationResource()
{
	std::vector<gr_surface>::iterator it;

	for (it = mSurfaces.begin(); it != mSurfaces.end(); ++it)
		res_free_surface(*it);

	mSurfaces.clear();
}

FontResource* ResourceManager::FindFont(const std::string& name) const
{
	for (std::vector<FontResource*>::const_iterator it = mFonts.begin(); it != mFonts.end(); ++it)
		if (name == (*it)->GetName())
			return *it;
	return NULL;
}

ImageResource* ResourceManager::FindImage(const std::string& name) const
{
	for (std::vector<ImageResource*>::const_iterator it = mImages.begin(); it != mImages.end(); ++it)
		if (name == (*it)->GetName())
			return *it;
	return NULL;
}

AnimationResource* ResourceManager::FindAnimation(const std::string& name) const
{
	for (std::vector<AnimationResource*>::const_iterator it = mAnimations.begin(); it != mAnimations.end(); ++it)
		if (name == (*it)->GetName())
			return *it;
	return NULL;
}

std::string ResourceManager::FindString(const std::string& name) const
{
	//if (this != NULL) {
		std::map<std::string, string_resource_struct>::const_iterator it = mStrings.find(name);
		if (it != mStrings.end())
			return it->second.value;
		LOGERR("String resource '%s' not found. No default value.\n", name.c_str());
		PageManager::AddStringResource("NO DEFAULT", name, "[" + name + ("]"));
	/*} else {
		LOGINFO("String resources not loaded when looking for '%s'. No default value.\n", name.c_str());
	}*/
	return "[" + name + ("]");
}

std::string ResourceManager::FindString(const std::string& name, const std::string& default_string) const
{
	//if (this != NULL) {
		std::map<std::string, string_resource_struct>::const_iterator it = mStrings.find(name);
		if (it != mStrings.end())
			return it->second.value;
		LOGERR("String resource '%s' not found. Using default value.\n", name.c_str());
		PageManager::AddStringResource("DEFAULT", name, default_string);
	/*} else {
		LOGINFO("String resources not loaded when looking for '%s'. Using default value.\n", name.c_str());
	}*/
	return default_string;
}

void ResourceManager::DumpStrings() const
{
	/*if (this == NULL) {
		gui_print("No string resources\n");
		return;
	}*/
	std::map<std::string, string_resource_struct>::const_iterator it;
	gui_print("Dumping all strings:\n");
	for (it = mStrings.begin(); it != mStrings.end(); it++)
		gui_print("source: %s: '%s' = '%s'\n", it->second.source.c_str(), it->first.c_str(), it->second.value.c_str());
	gui_print("Done dumping strings\n");
}

ResourceManager::ResourceManager()
{
}

void ResourceManager::AddStringResource(std::string resource_source, std::string resource_name, std::string value)
{
	string_resource_struct res;
	res.source = resource_source;
	res.value = value;
	mStrings[resource_name] = res;
}

void ResourceManager::LoadResources(xml_node<>* resList, ZipWrap* pZip, std::string resource_source)
{
	if (!resList)
		return;

	for (xml_node<>* child = resList->first_node(); child; child = child->next_sibling())
	{
		std::string type = child->name();
		if (type == "resource") {
			// legacy format : <resource type="...">
			xml_attribute<>* attr = child->first_attribute("type");
			type = attr ? attr->value() : "*unspecified*";
		}

		bool error = false;
		if (type == "font")
		{
			FontResource* res = new FontResource(child, pZip);
			if (res && res->GetResource())
				mFonts.push_back(res);
			else {
				error = true;
				delete res;
			}
		}
		else if (type == "fontoverride")
		{
			if (mFonts.size() != 0 && child && child->first_attribute("name")) {
				string FontName = child->first_attribute("name")->value();
				size_t font_count = mFonts.size(), i;
				bool found = false;

				for (i = 0; i < font_count; i++) {
					if (mFonts[i]->GetName() == FontName) {
						mFonts[i]->Override(child, pZip);
						found = true;
						break;
					}
				}
				if (!found) {
					LOGERR("Unable to locate font '%s' for override.\n", FontName.c_str());
				}
			} else if (mFonts.size() != 0)
				LOGERR("Unable to locate font name for type fontoverride.\n");
		}
		else if (type == "image")
		{
			ImageResource* res = new ImageResource(child, pZip);
			if (res && res->GetResource() && res->GetCanvas())
				mImages.push_back(res);
			else {
				error = true;
				delete res;
			}
		}
		else if (type == "animation")
		{
			AnimationResource* res = new AnimationResource(child, pZip);
			if (res && res->GetResourceCount() && res->GetCanvasCount())
				mAnimations.push_back(res);
			else {
				error = true;
				delete res;
			}
		}
		else if (type == "string")
		{
			if (xml_attribute<>* attr = child->first_attribute("name")) {
				string_resource_struct res;
				res.source = resource_source;
				res.value = child->value();
				mStrings[attr->value()] = res;
			} else
				error = true;
		}
		else
		{
			LOGERR("Resource type (%s) not supported.\n", type.c_str());
			error = true;
		}

		if (error)
		{
			std::string res_name;
			if (child->first_attribute("name"))
				res_name = child->first_attribute("name")->value();
			if (res_name.empty() && child->first_attribute("filename"))
				res_name = child->first_attribute("filename")->value();

			if (!res_name.empty()) {
				LOGERR("Resource (%s)-(%s) failed to load\n", type.c_str(), res_name.c_str());
			} else
				LOGERR("Resource type (%s) failed to load\n", type.c_str());
		}
	}
}

ResourceManager::~ResourceManager()
{
	for (std::vector<FontResource*>::iterator it = mFonts.begin(); it != mFonts.end(); ++it)
		delete *it;

	for (std::vector<ImageResource*>::iterator it = mImages.begin(); it != mImages.end(); ++it)
		delete *it;

	for (std::vector<AnimationResource*>::iterator it = mAnimations.begin(); it != mAnimations.end(); ++it)
		delete *it;
}
