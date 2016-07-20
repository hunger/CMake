/*============================================================================
  CMake - Cross Platform Makefile Generator
  Copyright 2004-2009 Kitware, Inc.
  Copyright 2004 Alexander Neundorf (neundorf@kde.org)

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/
#ifndef cmExtraCodeBlocksGenerator_h
#define cmExtraCodeBlocksGenerator_h

#include "cmExternalMakefileProjectGenerator.h"

class cmLocalGenerator;
class cmMakefile;
class cmGeneratorTarget;
class cmXMLWriter;

/** \class cmExtraCodeBlocksGenerator
 * \brief Write CodeBlocks project files for Makefile based projects
 */
class cmExtraCodeBlocksGenerator : public cmExternalMakefileProjectGenerator
{
public:
  cmExtraCodeBlocksGenerator();

  static cmExternalMakefileProjectGeneratorFactory *NewFactory();

  void Generate() CM_OVERRIDE;

private:
  struct CbpUnit
  {
    std::vector<const cmGeneratorTarget*> Targets;
  };

  void CreateProjectFile(const std::vector<cmLocalGenerator*>& lgs);

  void CreateNewProjectFile(const std::vector<cmLocalGenerator*>& lgs,
                            const std::string& filename);
  std::string CreateDummyTargetFile(cmLocalGenerator* lg,
                                    cmGeneratorTarget* target) const;

  std::string GetCBCompilerId(const cmMakefile* mf);
  int GetCBTargetType(cmGeneratorTarget* target);
  std::string BuildMakeCommand(const std::string& make, const char* makefile,
                               const std::string& target,
                               const std::string& makeFlags);
  void AppendTarget(cmXMLWriter& xml, const std::string& targetName,
                    cmGeneratorTarget* target, const char* make,
                    const cmLocalGenerator* lg, const char* compiler,
                    const std::string& makeFlags);
};

#endif
