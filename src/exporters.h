#pragma once
#include "document.h"
#include <string>
#include <vector>

std::string NodeToCSS(Document& doc, Node* n, bool withChildren);
std::string NodeToSVG(Document& doc, Node* n);

bool LooksLikeCSS(const std::string& s);
// Applies CSS declarations ("width: 10px; background: #fff; ...") to a node. Returns number of applied properties.
int ApplyCSS(Node* n, const std::string& css);

std::string AddAsset(Document& doc, int kind, const std::string& name, std::vector<unsigned char> data);
Node* ImportSVG(Document& doc, const std::string& svg, const std::string& name, Node* parent, ImVec2 localPos);
bool IsGif(const std::vector<unsigned char>& data);
Node* ImportVideo(Document& doc, const std::vector<unsigned char>& bytes, const std::string& name, Node* parent, ImVec2 localPos);
// Generates a tileable pattern asset: 0 noise, 1 scanlines, 2 grid, 3 stripes. Returns the asset id.
std::string MakeTextureAsset(Document& doc, int pattern);
Node* ImportImage(Document& doc, const std::vector<unsigned char>& bytes, const std::string& name, Node* parent, ImVec2 localPos);

std::string SanitizeIdent(const std::string& s, const char* fallback = "item");
