/*=========================================================================

  Program:   CMake - Cross-Platform Makefile Generator
  Module:    $RCSfile$
  Language:  C++
  Date:      $Date$
  Version:   $Revision$

  Copyright (c) 2002 Kitware, Inc., Insight Consortium.  All rights reserved.
  See Copyright.txt or http://www.cmake.org/HTML/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#include "cmFileCommand.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <cmsys/Directory.hxx>
#include <cmsys/Glob.hxx>
#include <cmsys/RegularExpression.hxx>

// Table of permissions flags.
#if defined(_WIN32) && !defined(__CYGWIN__)
static mode_t mode_owner_read = S_IREAD;
static mode_t mode_owner_write = S_IWRITE;
static mode_t mode_owner_execute = S_IEXEC;
static mode_t mode_group_read = 0;
static mode_t mode_group_write = 0;
static mode_t mode_group_execute = 0;
static mode_t mode_world_read = 0;
static mode_t mode_world_write = 0;
static mode_t mode_world_execute = 0;
static mode_t mode_setuid = 0;
static mode_t mode_setgid = 0;
#else
static mode_t mode_owner_read = S_IRUSR;
static mode_t mode_owner_write = S_IWUSR;
static mode_t mode_owner_execute = S_IXUSR;
static mode_t mode_group_read = S_IRGRP;
static mode_t mode_group_write = S_IWGRP;
static mode_t mode_group_execute = S_IXGRP;
static mode_t mode_world_read = S_IROTH;
static mode_t mode_world_write = S_IWOTH;
static mode_t mode_world_execute = S_IXOTH;
static mode_t mode_setuid = S_ISUID;
static mode_t mode_setgid = S_ISGID;
#endif

// cmLibraryCommand
bool cmFileCommand::InitialPass(std::vector<std::string> const& args)
{
  if(args.size() < 2 )
    {
    this->SetError("must be called with at least two arguments.");
    return false;
    }
  std::string subCommand = args[0];
  if ( subCommand == "WRITE" )
    {
    return this->HandleWriteCommand(args, false);
    }
  else if ( subCommand == "APPEND" )
    {
    return this->HandleWriteCommand(args, true);
    }
  else if ( subCommand == "READ" )
    {
    return this->HandleReadCommand(args);
    }
  else if ( subCommand == "STRINGS" )
    {
    return this->HandleStringsCommand(args);
    }
  else if ( subCommand == "GLOB" )
    {
    return this->HandleGlobCommand(args, false);
    }
  else if ( subCommand == "GLOB_RECURSE" )
    {
    return this->HandleGlobCommand(args, true);
    }
  else if ( subCommand == "MAKE_DIRECTORY" )
    {
    return this->HandleMakeDirectoryCommand(args);
    }
  else if ( subCommand == "REMOVE" )
    {
    return this->HandleRemove(args, false);
    }
  else if ( subCommand == "REMOVE_RECURSE" )
    {
    return this->HandleRemove(args, true);
    }
  else if ( subCommand == "INSTALL" )
    {
    return this->HandleInstallCommand(args);
    }
  else if ( subCommand == "RELATIVE_PATH" )
    {
    return this->HandleRelativePathCommand(args);
    }
  else if ( subCommand == "TO_CMAKE_PATH" )
    {
    return this->HandleCMakePathCommand(args, false);
    }
  else if ( subCommand == "TO_NATIVE_PATH" )
    {
    return this->HandleCMakePathCommand(args, true);
    }

  std::string e = "does not recognize sub-command "+subCommand;
  this->SetError(e.c_str());
  return false;
}

//----------------------------------------------------------------------------
bool cmFileCommand::HandleWriteCommand(std::vector<std::string> const& args,
  bool append)
{
  std::string message;
  std::vector<std::string>::const_iterator i = args.begin();

  i++; // Get rid of subcommand

  std::string fileName = *i;
  if ( !cmsys::SystemTools::FileIsFullPath(i->c_str()) )
    {
    fileName = this->Makefile->GetCurrentDirectory();
    fileName += "/" + *i;
    }

  i++;

  for(;i != args.end(); ++i)
    {
    message += *i;
    }
  if ( !this->Makefile->CanIWriteThisFile(fileName.c_str()) )
    {
    std::string e
      = "attempted to write a file: " + fileName +
      " into a source directory.";
    this->SetError(e.c_str());
    cmSystemTools::SetFatalErrorOccured();
    return false;
    }
  std::string dir = cmSystemTools::GetFilenamePath(fileName);
  cmSystemTools::MakeDirectory(dir.c_str());

  mode_t mode =
#if defined( _MSC_VER ) || defined( __MINGW32__ )
    S_IREAD | S_IWRITE
#elif defined( __BORLANDC__ )
    S_IRUSR | S_IWUSR
#else
    S_IRUSR | S_IWUSR |
    S_IRGRP |
    S_IROTH
#endif
    ;

  // Set permissions to writable
  if ( cmSystemTools::GetPermissions(fileName.c_str(), mode) )
    {
    cmSystemTools::SetPermissions(fileName.c_str(),
#if defined( _MSC_VER ) || defined( __MINGW32__ )
      S_IREAD | S_IWRITE
#else
      S_IRUSR | S_IWUSR
#endif
    );
    }
  // If GetPermissions fails, pretend like it is ok. File open will fail if
  // the file is not writable
  std::ofstream file(fileName.c_str(), append?std::ios::app: std::ios::out);
  if ( !file )
    {
    std::string error = "Internal CMake error when trying to open file: ";
    error += fileName.c_str();
    error += " for writing.";
    this->SetError(error.c_str());
    return false;
    }
  file << message;
  file.close();
  cmSystemTools::SetPermissions(fileName.c_str(), mode);
  this->Makefile->AddWrittenFile(fileName.c_str());
  return true;
}

//----------------------------------------------------------------------------
bool cmFileCommand::HandleReadCommand(std::vector<std::string> const& args)
{
  if ( args.size() < 3 )
    {
    this->SetError("READ must be called with at least two additional "
                   "arguments");
    return false;
    }

  std::string fileName = args[1];
  if ( !cmsys::SystemTools::FileIsFullPath(args[1].c_str()) )
    {
    fileName = this->Makefile->GetCurrentDirectory();
    fileName += "/" + args[1];
    }

  std::string variable = args[2];
  std::ifstream file(fileName.c_str(), std::ios::in);
  if ( !file )
    {
    std::string error = "Internal CMake error when trying to open file: ";
    error += fileName.c_str();
    error += " for reading.";
    this->SetError(error.c_str());
    return false;
    }

  // if there a limit?
  long sizeLimit = -1;
  if (args.size() >= 5 && args[3] == "LIMIT")
    {
    sizeLimit = atoi(args[4].c_str());
    }

  std::string output;
  std::string line;
  bool has_newline = false;
  while (sizeLimit != 0 &&
         cmSystemTools::GetLineFromStream(file, line, &has_newline, 
                                          sizeLimit) )
    {
    if (sizeLimit > 0)
      {
      sizeLimit = sizeLimit - static_cast<long>(line.size());
      if (has_newline)
        {
        sizeLimit--;
        }
      if (sizeLimit < 0)
        {
        sizeLimit = 0;
        }
      }
    output += line;
    if ( has_newline )
      {
      output += "\n";
      }
    }
  this->Makefile->AddDefinition(variable.c_str(), output.c_str());
  return true;
}

//----------------------------------------------------------------------------
bool cmFileCommand::HandleStringsCommand(std::vector<std::string> const& args)
{
  if(args.size() < 3)
    {
    this->SetError("STRINGS requires a file name and output variable");
    return false;
    }

  // Get the file to read.
  std::string fileName = args[1];
  if(!cmsys::SystemTools::FileIsFullPath(fileName.c_str()))
    {
    fileName = this->Makefile->GetCurrentDirectory();
    fileName += "/" + args[1];
    }

  // Get the variable in which to store the results.
  std::string outVar = args[2];

  // Parse the options.
  enum { arg_none,
         arg_limit_input,
         arg_limit_output,
         arg_limit_count,
         arg_length_minimum,
         arg_length_maximum,
         arg__maximum,
         arg_regex };
  unsigned int minlen = 0;
  unsigned int maxlen = 0;
  int limit_input = -1;
  int limit_output = -1;
  unsigned int limit_count = 0;
  cmsys::RegularExpression regex;
  bool have_regex = false;
  bool newline_consume = false;
  int arg_mode = arg_none;
  for(unsigned int i=3; i < args.size(); ++i)
    {
    if(args[i] == "LIMIT_INPUT")
      {
      arg_mode = arg_limit_input;
      }
    else if(args[i] == "LIMIT_OUTPUT")
      {
      arg_mode = arg_limit_output;
      }
    else if(args[i] == "LIMIT_COUNT")
      {
      arg_mode = arg_limit_count;
      }
    else if(args[i] == "LENGTH_MINIMUM")
      {
      arg_mode = arg_length_minimum;
      }
    else if(args[i] == "LENGTH_MAXIMUM")
      {
      arg_mode = arg_length_maximum;
      }
    else if(args[i] == "REGEX")
      {
      arg_mode = arg_regex;
      }
    else if(args[i] == "NEWLINE_CONSUME")
      {
      newline_consume = true;
      arg_mode = arg_none;
      }
    else if(arg_mode == arg_limit_input)
      {
      if(sscanf(args[i].c_str(), "%d", &limit_input) != 1 ||
         limit_input < 0)
        {
        cmOStringStream e;
        e << "STRINGS option LIMIT_INPUT value \""
          << args[i] << "\" is not an unsigned integer.";
        this->SetError(e.str().c_str());
        return false;
        }
      arg_mode = arg_none;
      }
    else if(arg_mode == arg_limit_output)
      {
      if(sscanf(args[i].c_str(), "%d", &limit_output) != 1 ||
         limit_output < 0)
        {
        cmOStringStream e;
        e << "STRINGS option LIMIT_OUTPUT value \""
          << args[i] << "\" is not an unsigned integer.";
        this->SetError(e.str().c_str());
        return false;
        }
      arg_mode = arg_none;
      }
    else if(arg_mode == arg_limit_count)
      {
      int count;
      if(sscanf(args[i].c_str(), "%d", &count) != 1 || count < 0)
        {
        cmOStringStream e;
        e << "STRINGS option LIMIT_COUNT value \""
          << args[i] << "\" is not an unsigned integer.";
        this->SetError(e.str().c_str());
        return false;
        }
      limit_count = count;
      arg_mode = arg_none;
      }
    else if(arg_mode == arg_length_minimum)
      {
      int len;
      if(sscanf(args[i].c_str(), "%d", &len) != 1 || len < 0)
        {
        cmOStringStream e;
        e << "STRINGS option LENGTH_MINIMUM value \""
          << args[i] << "\" is not an unsigned integer.";
        this->SetError(e.str().c_str());
        return false;
        }
      minlen = len;
      arg_mode = arg_none;
      }
    else if(arg_mode == arg_length_maximum)
      {
      int len;
      if(sscanf(args[i].c_str(), "%d", &len) != 1 || len < 0)
        {
        cmOStringStream e;
        e << "STRINGS option LENGTH_MAXIMUM value \""
          << args[i] << "\" is not an unsigned integer.";
        this->SetError(e.str().c_str());
        return false;
        }
      maxlen = len;
      arg_mode = arg_none;
      }
    else if(arg_mode == arg_regex)
      {
      if(!regex.compile(args[i].c_str()))
        {
        cmOStringStream e;
        e << "STRINGS option REGEX value \""
          << args[i] << "\" could not be compiled.";
        this->SetError(e.str().c_str());
        return false;
        }
      have_regex = true;
      arg_mode = arg_none;
      }
    else
      {
      cmOStringStream e;
      e << "STRINGS given unknown argument \""
        << args[i] << "\"";
      this->SetError(e.str().c_str());
      return false;
      }
    }

  // Open the specified file.
#if defined(_WIN32) || defined(__CYGWIN__)
  std::ifstream fin(fileName.c_str(), std::ios::in | std::ios::binary);
#else
  std::ifstream fin(fileName.c_str(), std::ios::in);
#endif
  if(!fin)
    {
    cmOStringStream e;
    e << "STRINGS file \"" << fileName << "\" cannot be read.";
    this->SetError(e.str().c_str());
    return false;
    }

  // Parse strings out of the file.
  int output_size = 0;
  std::vector<std::string> strings;
  std::string s;
  int c;
  while((!limit_count || strings.size() < limit_count) &&
        (limit_input < 0 || static_cast<int>(fin.tellg()) < limit_input) &&
        (c = fin.get(), fin))
    {
    if(c == '\0')
      {
      // A terminating null character has been found.  Check if the
      // current string matches the requirements.  Since it was
      // terminated by a null character, we require that the length be
      // at least one no matter what the user specified.
      if(s.length() >= minlen && s.length() >= 1 &&
         (!have_regex || regex.find(s.c_str())))
        {
        output_size += static_cast<int>(s.size()) + 1;
        if(limit_output >= 0 && output_size >= limit_output)
          {
          s = "";
          break;
          }
        strings.push_back(s);
        }

      // Reset the string to empty.
      s = "";
      }
    else if(c == '\n' && !newline_consume)
      {
      // The current line has been terminated.  Check if the current
      // string matches the requirements.  The length may now be as
      // low as zero since blank lines are allowed.
      if(s.length() >= minlen &&
         (!have_regex || regex.find(s.c_str())))
        {
        output_size += static_cast<int>(s.size()) + 1;
        if(limit_output >= 0 && output_size >= limit_output)
          {
          s = "";
          break;
          }
        strings.push_back(s);
        }

      // Reset the string to empty.
      s = "";
      }
    else if(c == '\r')
      {
      // Ignore CR character to make output always have UNIX newlines.
      }
    else if(c >= 0x20 && c < 0x7F || c == '\t' ||
            (c == '\n' && newline_consume))
      {
      // This is an ASCII character that may be part of a string.
      s += c;
      }
    else
      {
      // This is a non-string character.  Reset the string to emtpy.
      s = "";
      }

    // Terminate a string if the maximum length is reached.
    if(maxlen > 0 && s.size() == maxlen)
      {
      if(s.length() >= minlen &&
         (!have_regex || regex.find(s.c_str())))
        {
        output_size += static_cast<int>(s.size()) + 1;
        if(limit_output >= 0 && output_size >= limit_output)
          {
          s = "";
          break;
          }
        strings.push_back(s);
        }
      s = "";
      }
    }

  // If there is a non-empty current string we have hit the end of the
  // input file or the input size limit.  Check if the current string
  // matches the requirements.
  if((!limit_count || strings.size() < limit_count) &&
     !s.empty() && s.length() >= minlen &&
     (!have_regex || regex.find(s.c_str())))
    {
    output_size += static_cast<int>(s.size()) + 1;
    if(limit_output < 0 || output_size < limit_output)
      {
      strings.push_back(s);
      }
    }

  // Encode the result in a CMake list.
  const char* sep = "";
  std::string output;
  for(std::vector<std::string>::const_iterator si = strings.begin();
      si != strings.end(); ++si)
    {
    // Separate the strings in the output to make it a list.
    output += sep;
    sep = ";";

    // Store the string in the output, but escape semicolons to
    // make sure it is a list.
    std::string const& sr = *si;
    for(unsigned int i=0; i < sr.size(); ++i)
      {
      if(sr[i] == ';')
        {
        output += '\\';
        }
      output += sr[i];
      }
    }

  // Save the output in a makefile variable.
  this->Makefile->AddDefinition(outVar.c_str(), output.c_str());
  return true;
}

//----------------------------------------------------------------------------
bool cmFileCommand::HandleGlobCommand(std::vector<std::string> const& args,
  bool recurse)
{
  if ( args.size() < 2 )
    {
    this->SetError("GLOB requires at least a variable name");
    return false;
    }

  std::vector<std::string>::const_iterator i = args.begin();

  i++; // Get rid of subcommand

  std::string variable = *i;
  i++;
  cmsys::Glob g;
  g.SetRecurse(recurse);
  std::string output = "";
  bool first = true;
  for ( ; i != args.end(); ++i )
    {
    if ( *i == "RELATIVE" )
      {
      ++i; // skip RELATIVE
      if ( i == args.end() )
        {
        this->SetError("GLOB requires a directory after the RELATIVE tag");
        return false;
        }
      g.SetRelative(i->c_str());
      ++i;
      if(i == args.end())
        {
        this->SetError("GLOB requires a glob expression after the directory");
        return false;
        }
      }
    if ( !cmsys::SystemTools::FileIsFullPath(i->c_str()) )
      {
      std::string expr = this->Makefile->GetCurrentDirectory();
      // Handle script mode
      if ( expr.size() > 0 )
        {
        expr += "/" + *i;
        g.FindFiles(expr);
        }
      else
        {
        g.FindFiles(*i);
        }
      }
    else
      {
      g.FindFiles(*i);
      }
    std::vector<std::string>::size_type cc;
    std::vector<std::string>& files = g.GetFiles();
    for ( cc = 0; cc < files.size(); cc ++ )
      {
      if ( !first )
        {
        output += ";";
        }
      output += files[cc];
      first = false;
      }
    }
  this->Makefile->AddDefinition(variable.c_str(), output.c_str());
  return true;
}

//----------------------------------------------------------------------------
bool cmFileCommand::HandleMakeDirectoryCommand(
  std::vector<std::string> const& args)
{
  if(args.size() < 2 )
    {
    this->SetError("called with incorrect number of arguments");
    return false;
    }

  std::vector<std::string>::const_iterator i = args.begin();

  i++; // Get rid of subcommand

  std::string expr;
  for ( ; i != args.end(); ++i )
    {
    const std::string* cdir = &(*i);
    if ( !cmsys::SystemTools::FileIsFullPath(i->c_str()) )
      {
      expr = this->Makefile->GetCurrentDirectory();
      expr += "/" + *i;
      cdir = &expr;
      }
    if ( !this->Makefile->CanIWriteThisFile(cdir->c_str()) )
      {
      std::string e = "attempted to create a directory: " + *cdir
        + " into a source directory.";
      this->SetError(e.c_str());
      cmSystemTools::SetFatalErrorOccured();
      return false;
      }
    if ( !cmSystemTools::MakeDirectory(cdir->c_str()) )
      {
      std::string error = "problem creating directory: " + *cdir;
      this->SetError(error.c_str());
      return false;
      }
    }
  return true;
}

//----------------------------------------------------------------------------
// File installation helper class.
struct cmFileInstaller
{
  // Methods to actually install files.
  bool InstallFile(const char* fromFile, const char* toFile, bool always);
  bool InstallDirectory(const char* source, const char* destination,
                        bool always);

  // All instances need the file command and makefile using them.
  cmFileInstaller(cmFileCommand* fc, cmMakefile* mf):
    FileCommand(fc), Makefile(mf), DestDirLength(0)
    {
    // Get the current manifest.
    this->Manifest =
      this->Makefile->GetSafeDefinition("CMAKE_INSTALL_MANIFEST_FILES");
    }
  ~cmFileInstaller()
    {
    // Save the updated install manifest.
    this->Makefile->AddDefinition("CMAKE_INSTALL_MANIFEST_FILES",
                                  this->Manifest.c_str());
    }

private:
  cmFileCommand* FileCommand;
  cmMakefile* Makefile;
public:

  // The length of the destdir setting.
  int DestDirLength;

  // The current file manifest (semicolon separated list).
  std::string Manifest;

  // Permissions for files and directories installed by this object.
  mode_t FilePermissions;
  mode_t DirPermissions;

  // Properties set by pattern and regex match rules.
  struct MatchProperties
  {
    bool Exclude;
    mode_t Permissions;
    MatchProperties(): Exclude(false), Permissions(0) {}
  };
  struct MatchRule
  {
    cmsys::RegularExpression Regex;
    MatchProperties Properties;
    std::string RegexString;
    MatchRule(std::string const& regex):
      Regex(regex.c_str()), RegexString(regex) {}
  };
  std::vector<MatchRule> MatchRules;

  // Get the properties from rules matching this input file.
  MatchProperties CollectMatchProperties(const char* file)
    {
    // Match rules are case-insensitive on some platforms.
#if defined(_WIN32) || defined(__APPLE__) || defined(__CYGWIN__)
    std::string lower = cmSystemTools::LowerCase(file);
    file = lower.c_str();
#endif

    // Collect properties from all matching rules.
    MatchProperties result;
    for(std::vector<MatchRule>::iterator mr = this->MatchRules.begin();
        mr != this->MatchRules.end(); ++mr)
      {
      if(mr->Regex.find(file))
        {
        result.Exclude |= mr->Properties.Exclude;
        result.Permissions |= mr->Properties.Permissions;
        }
      }
    return result;
    }

  // Append a file to the installation manifest.
  void ManifestAppend(std::string const& file)
    {
    this->Manifest += ";";
    this->Manifest += file.substr(this->DestDirLength);
    }

  // Translate an argument to a permissions bit.
  bool CheckPermissions(std::string const& arg, mode_t& permissions)
    {
    if(arg == "OWNER_READ")         { permissions |= mode_owner_read; }
    else if(arg == "OWNER_WRITE")   { permissions |= mode_owner_write; }
    else if(arg == "OWNER_EXECUTE") { permissions |= mode_owner_execute; }
    else if(arg == "GROUP_READ")    { permissions |= mode_group_read; }
    else if(arg == "GROUP_WRITE")   { permissions |= mode_group_write; }
    else if(arg == "GROUP_EXECUTE") { permissions |= mode_group_execute; }
    else if(arg == "WORLD_READ")    { permissions |= mode_world_read; }
    else if(arg == "WORLD_WRITE")   { permissions |= mode_world_write; }
    else if(arg == "WORLD_EXECUTE") { permissions |= mode_world_execute; }
    else if(arg == "SETUID")        { permissions |= mode_setuid; }
    else if(arg == "SETGID")        { permissions |= mode_setgid; }
    else
      {
      cmOStringStream e;
      e << "INSTALL given invalid permission \"" << arg << "\".";
      this->FileCommand->SetError(e.str().c_str());
      return false;
      }
    return true;
    }

private:
  bool InstallSymlink(const char* fromFile, const char* toFile, bool always);
};

//----------------------------------------------------------------------------
bool cmFileInstaller::InstallSymlink(const char* fromFile, const char* toFile,
                                     bool always)
{
  // Inform the user about this file installation.
  std::string message = "Installing ";
  message += toFile;
  this->Makefile->DisplayStatus(message.c_str(), -1);

  // Read the original symlink.
  std::string symlinkTarget;
  if(!cmSystemTools::ReadSymlink(fromFile, symlinkTarget))
    {
    cmOStringStream e;
    e << "INSTALL cannot read symlink \"" << fromFile
      << "\" to duplicate at \"" << toFile << "\".";
    this->FileCommand->SetError(e.str().c_str());
    return false;
    }

  // Compare the symlink value to that at the destination if not
  // always installing.
  if(!always)
    {
    std::string oldSymlinkTarget;
    if(cmSystemTools::ReadSymlink(toFile, oldSymlinkTarget))
      {
      if(symlinkTarget == oldSymlinkTarget)
        {
        return true;
        }
      }
    }

  // Remove the destination file so we can always create the symlink.
  cmSystemTools::RemoveFile(toFile);

  // Create the symlink.
  if(!cmSystemTools::CreateSymlink(symlinkTarget.c_str(), toFile))
    {
    cmOStringStream e;
    e << "INSTALL cannot duplicate symlink \"" << fromFile
      << "\" at \"" << toFile << "\".";
    this->FileCommand->SetError(e.str().c_str());
    return false;
    }

  // Add the file to the manifest.
  this->ManifestAppend(toFile);

  return true;
}

//----------------------------------------------------------------------------
bool cmFileInstaller::InstallFile(const char* fromFile, const char* toFile,
                                  bool always)
{
  // Collect any properties matching this file name.
  MatchProperties match_properties = this->CollectMatchProperties(fromFile);

  // Skip the file if it is excluded.
  if(match_properties.Exclude)
    {
    return true;
    }

  // Short-circuit for symbolic links.
  if(cmSystemTools::FileIsSymlink(fromFile))
    {
    return this->InstallSymlink(fromFile, toFile, always);
    }

  // Inform the user about this file installation.
  std::string message = "Installing ";
  message += toFile;
  this->Makefile->DisplayStatus(message.c_str(), -1);

  // Copy the file.
  if(!cmSystemTools::CopyAFile(fromFile, toFile, always))
    {
    cmOStringStream e;
    e << "INSTALL cannot copy file \"" << fromFile
      << "\" to \"" << toFile << "\".";
    this->FileCommand->SetError(e.str().c_str());
    return false;
    }

  // Add the file to the manifest.
  this->ManifestAppend(toFile);

  // Set permissions of the destination file.
  mode_t permissions = (match_properties.Permissions?
                        match_properties.Permissions : this->FilePermissions);
  if(!permissions)
    {
    // No permissions were explicitly provided but the user requested
    // that the source file permissions be used.
    cmSystemTools::GetPermissions(fromFile, permissions);
    }
  if(permissions && !cmSystemTools::SetPermissions(toFile, permissions))
    {
    cmOStringStream e;
    e << "Problem setting permissions on file \"" << toFile << "\"";
    this->FileCommand->SetError(e.str().c_str());
    return false;
    }

  return true;
}

//----------------------------------------------------------------------------
bool cmFileInstaller::InstallDirectory(const char* source,
                                       const char* destination,
                                       bool always)
{
  // Collect any properties matching this directory name.
  MatchProperties match_properties = this->CollectMatchProperties(source);

  // Skip the directory if it is excluded.
  if(match_properties.Exclude)
    {
    return true;
    }

  // Short-circuit for symbolic links.
  if(cmSystemTools::FileIsSymlink(source))
    {
    return this->InstallSymlink(source, destination, always);
    }

  // Inform the user about this directory installation.
  std::string message = "Installing ";
  message += destination;
  this->Makefile->DisplayStatus(message.c_str(), -1);

  // Make sure the destination directory exists.
  if(!cmSystemTools::MakeDirectory(destination))
    {
    return false;
    }

  // Compute the requested permissions for the destination directory.
  mode_t permissions = (match_properties.Permissions?
                        match_properties.Permissions : this->DirPermissions);
  if(!permissions)
    {
    // No permissions were explicitly provided but the user requested
    // that the source directory permissions be used.
    cmSystemTools::GetPermissions(source, permissions);
    }

  // Compute the set of permissions required on this directory to
  // recursively install files and subdirectories safely.
  mode_t required_permissions =
    mode_owner_read | mode_owner_write | mode_owner_execute;

  // If the required permissions are specified it is safe to set the
  // final permissions now.  Otherwise we must add the required
  // permissions temporarily during file installation.
  mode_t permissions_before = 0;
  mode_t permissions_after = 0;
  if(permissions & required_permissions)
    {
    permissions_before = permissions;
    }
  else
    {
    permissions_before = permissions | required_permissions;
    permissions_after = permissions;
    }

  // Set the required permissions of the destination directory.
  if(permissions_before &&
     !cmSystemTools::SetPermissions(destination, permissions_before))
    {
    cmOStringStream e;
    e << "Problem setting permissions on directory \""
      << destination << "\"";
    this->FileCommand->SetError(e.str().c_str());
    return false;
    }

  // Load the directory contents to traverse it recursively.
  cmsys::Directory dir;
  if(source && *source)
    {
    dir.Load(source);
    }
  unsigned long numFiles = static_cast<unsigned long>(dir.GetNumberOfFiles());
  for(unsigned long fileNum = 0; fileNum < numFiles; ++fileNum)
    {
    if(!(strcmp(dir.GetFile(fileNum), ".") == 0 ||
         strcmp(dir.GetFile(fileNum), "..") == 0))
      {
      cmsys_stl::string fromPath = source;
      fromPath += "/";
      fromPath += dir.GetFile(fileNum);
      if(cmSystemTools::FileIsDirectory(fromPath.c_str()))
        {
        cmsys_stl::string toDir = destination;
        toDir += "/";
        toDir += dir.GetFile(fileNum);
        if(!this->InstallDirectory(fromPath.c_str(), toDir.c_str(), always))
          {
          return false;
          }
        }
      else
        {
        // Install this file.
        std::string toFile = destination;
        toFile += "/";
        toFile += dir.GetFile(fileNum);
        if(!this->InstallFile(fromPath.c_str(), toFile.c_str(), always))
          {
          return false;
          }
        }
      }
    }

  // Set the requested permissions of the destination directory.
  if(permissions_after &&
     !cmSystemTools::SetPermissions(destination, permissions_after))
    {
    cmOStringStream e;
    e << "Problem setting permissions on directory \"" << destination << "\"";
    this->FileCommand->SetError(e.str().c_str());
    return false;
    }

  return true;
}

//----------------------------------------------------------------------------
void cmFileCommand::HandleInstallPermissions(cmFileInstaller& installer, 
                              mode_t& permissions_file,
                              mode_t& permissions_dir,
                              int itype,
                              bool use_given_permissions_file,
                              bool use_given_permissions_dir,
                              bool use_source_permissions) const
{
  // Choose a default for shared library permissions.
  bool install_so_no_exe = this->Makefile->IsOn("CMAKE_INSTALL_SO_NO_EXE");
  // If file permissions were not specified set default permissions
  // for this target type.
  if(!use_given_permissions_file && !use_source_permissions)
    {
    switch(itype)
      {
      case cmTarget::SHARED_LIBRARY:
      case cmTarget::MODULE_LIBRARY:
        if(install_so_no_exe)
          {
          // Use read/write permissions.
          permissions_file = 0;
          permissions_file |= mode_owner_read;
          permissions_file |= mode_owner_write;
          permissions_file |= mode_group_read;
          permissions_file |= mode_world_read;
          break;
          }
      case cmTarget::EXECUTABLE:
      case cmTarget::INSTALL_PROGRAMS:
        // Use read/write/executable permissions.
        permissions_file = 0;
        permissions_file |= mode_owner_read;
        permissions_file |= mode_owner_write;
        permissions_file |= mode_owner_execute;
        permissions_file |= mode_group_read;
        permissions_file |= mode_group_execute;
        permissions_file |= mode_world_read;
        permissions_file |= mode_world_execute;
        break;
      default:
        // Use read/write permissions.
        permissions_file = 0;
        permissions_file |= mode_owner_read;
        permissions_file |= mode_owner_write;
        permissions_file |= mode_group_read;
        permissions_file |= mode_world_read;
        break;
      }
    }

  // If directory permissions were not specified set default permissions.
  if(!use_given_permissions_dir && !use_source_permissions)
    {
    // Use read/write/executable permissions.
    permissions_dir = 0;
    permissions_dir |= mode_owner_read;
    permissions_dir |= mode_owner_write;
    permissions_dir |= mode_owner_execute;
    permissions_dir |= mode_group_read;
    permissions_dir |= mode_group_execute;
    permissions_dir |= mode_world_read;
    permissions_dir |= mode_world_execute;
    }
  // Set the installer permissions.
  installer.FilePermissions = permissions_file;
  installer.DirPermissions = permissions_dir;
}

//----------------------------------------------------------------------------
void cmFileCommand
::GetTargetTypeFromString(const std::string& stype, int& itype) const
{
  if ( stype == "EXECUTABLE" )
    {
    itype = cmTarget::EXECUTABLE;
    }
  else if ( stype == "PROGRAM" )
    {
    itype = cmTarget::INSTALL_PROGRAMS;
    }
  else if ( stype == "STATIC_LIBRARY" )
    {
    itype = cmTarget::STATIC_LIBRARY;
    }
  else if ( stype == "SHARED_LIBRARY" )
    {
    itype = cmTarget::SHARED_LIBRARY;
    }
  else if ( stype == "MODULE" )
    {
    itype = cmTarget::MODULE_LIBRARY;
    }
  else if ( stype == "DIRECTORY" )
    {
    itype = cmTarget::INSTALL_DIRECTORY;
    }
}


//----------------------------------------------------------------------------
bool cmFileCommand::HandleInstallDestination(cmFileInstaller& installer, 
                                             std::string& destination)
{
  if ( destination.size() < 2 )
    {
    this->SetError("called with inapropriate arguments. "
        "No DESTINATION provided or .");
    return false;
    }
    
  const char* destdir = cmSystemTools::GetEnv("DESTDIR");
  if ( destdir && *destdir )
    {
    std::string sdestdir = destdir;
    cmSystemTools::ConvertToUnixSlashes(sdestdir);

    char ch1 = destination[0];
    char ch2 = destination[1];
    char ch3 = 0;
    if ( destination.size() > 2 )
      {
      ch3 = destination[2];
      }
    int skip = 0;
    if ( ch1 != '/' )
      {
      int relative = 0;
      if ( ( ch1 >= 'a' && ch1 <= 'z' || ch1 >= 'A' && ch1 <= 'Z' ) &&
             ch2 == ':' )
        {
        // Assume windows
        // let's do some destdir magic:
        skip = 2;
        if ( ch3 != '/' )
          {
          relative = 1;
          }
        }
      else
        {
        relative = 1;
        }
      if ( relative )
        {
        // This is relative path on unix or windows. Since we are doing
        // destdir, this case does not make sense.
        this->SetError("called with relative DESTINATION. This "
            "does not make sense when using DESTDIR. Specify "
            "absolute path or remove DESTDIR environment variable.");
        return false;
        }
      }
    else
      {
      if ( ch2 == '/' )
        {
        // looks like a network path.
        this->SetError("called with network path DESTINATION. This "
            "does not make sense when using DESTDIR. Specify local "
            "absolute path or remove DESTDIR environment variable.");
        return false;
        }
      }
    destination = sdestdir + (destination.c_str() + skip);
    installer.DestDirLength = int(sdestdir.size());
    }

  if ( !cmSystemTools::FileExists(destination.c_str()) )
    {
    if ( !cmSystemTools::MakeDirectory(destination.c_str()) )
      {
      std::string errstring = "cannot create directory: " + destination +
          ". Maybe need administrative privileges.";
      this->SetError(errstring.c_str());
      return false;
      }
    }
  if ( !cmSystemTools::FileIsDirectory(destination.c_str()) )
    {
    std::string errstring = "INSTALL destination: " + destination +
        " is not a directory.";
    this->SetError(errstring.c_str());
    return false;
    }
  return true;
}

//----------------------------------------------------------------------------
bool cmFileCommand::HandleInstallCommand(std::vector<std::string> const& args)
{
  if ( args.size() < 6 )
    {
    this->SetError("called with incorrect number of arguments");
    return false;
    }

  // Construct a file installer object.
  cmFileInstaller installer(this, this->Makefile);

  std::string rename = "";
  std::string destination = "";

  std::set<cmStdString> components;
  std::set<cmStdString> configurations;
  std::vector<std::string> files;
  int itype = cmTarget::INSTALL_FILES;

  std::map<cmStdString, const char*> properties;
  bool optional = false;
  bool result = this->ParseInstallArgs(args, installer, components, 
                                       configurations, properties, 
                                       itype, rename, destination, files, 
                                       optional);
  if (result == true)
    {
    result = this->DoInstall(installer, components, configurations, properties,
                             itype, rename, destination, files, optional);
    }
  return result;
}

//----------------------------------------------------------------------------
bool cmFileCommand::ParseInstallArgs(std::vector<std::string> const& args,
                                cmFileInstaller& installer,
                                std::set<cmStdString>& components,
                                std::set<cmStdString>& configurations,
                                std::map<cmStdString, const char*>& properties,
                                int& itype,
                                std::string& rename,
                                std::string& destination,
                                std::vector<std::string>& files,
                                bool& optional)
{
    std::string stype = "FILES";
    bool doing_files = false;
    bool doing_properties = false;
    bool doing_permissions_file = false;
    bool doing_permissions_dir = false;
    bool doing_permissions_match = false;
    bool doing_components = false;
    bool doing_configurations = false;
    bool use_given_permissions_file = false;
    bool use_given_permissions_dir = false;
    bool use_source_permissions = false;
    mode_t permissions_file = 0;
    mode_t permissions_dir = 0;

    cmFileInstaller::MatchRule* current_match_rule = 0;
    std::vector<std::string>::size_type i = 0;
    i++; // Get rid of subcommand
    for ( ; i != args.size(); ++i )
      {
      const std::string* cstr = &args[i];
      if ( *cstr == "DESTINATION" && i < args.size()-1 )
        {
        if(current_match_rule)
          {
          cmOStringStream e;
          e << "INSTALL does not allow \"" << *cstr << "\" after REGEX.";
          this->SetError(e.str().c_str());
          return false;
          }

        i++;
        destination = args[i];
        doing_files = false;
        doing_properties = false;
        doing_permissions_file = false;
        doing_permissions_dir = false;
        doing_components = false;
        doing_configurations = false;
        }
      else if ( *cstr == "TYPE" && i < args.size()-1 )
        {
        if(current_match_rule)
          {
          cmOStringStream e;
          e << "INSTALL does not allow \"" << *cstr << "\" after REGEX.";
          this->SetError(e.str().c_str());
          return false;
          }

        i++;
        stype = args[i];
        if ( args[i+1] == "OPTIONAL" )
          {
          i++;
          optional = true;
          }
        doing_properties = false;
        doing_files = false;
        doing_permissions_file = false;
        doing_permissions_dir = false;
        doing_components = false;
        doing_configurations = false;
        }
      else if ( *cstr == "RENAME" && i < args.size()-1 )
        {
        if(current_match_rule)
          {
          cmOStringStream e;
          e << "INSTALL does not allow \"" << *cstr << "\" after REGEX.";
          this->SetError(e.str().c_str());
          return false;
          }

        i++;
        rename = args[i];
        doing_properties = false;
        doing_files = false;
        doing_permissions_file = false;
        doing_permissions_dir = false;
        doing_components = false;
        doing_configurations = false;
        }
      else if ( *cstr == "REGEX" && i < args.size()-1 )
        {
        i++;
        installer.MatchRules.push_back(cmFileInstaller::MatchRule(args[i]));
        current_match_rule = &*(installer.MatchRules.end()-1);
        if(!current_match_rule->Regex.is_valid())
          {
          cmOStringStream e;
          e << "INSTALL could not compile REGEX \"" << args[i] << "\".";
          this->SetError(e.str().c_str());
          return false;
          }
        doing_properties = false;
        doing_files = false;
        doing_permissions_file = false;
        doing_permissions_dir = false;
        doing_components = false;
        doing_configurations = false;
        }
      else if ( *cstr == "EXCLUDE"  )
        {
      // Add this property to the current match rule.
        if(!current_match_rule)
          {
          cmOStringStream e;
          e << "INSTALL does not allow \""
              << *cstr << "\" before a REGEX is given.";
          this->SetError(e.str().c_str());
          return false;
          }
        current_match_rule->Properties.Exclude = true;
        doing_permissions_match = true;
        }
      else if ( *cstr == "PROPERTIES"  )
        {
        if(current_match_rule)
          {
          cmOStringStream e;
          e << "INSTALL does not allow \"" << *cstr << "\" after REGEX.";
          this->SetError(e.str().c_str());
          return false;
          }

        doing_properties = true;
        doing_files = false;
        doing_permissions_file = false;
        doing_permissions_dir = false;
        doing_components = false;
        doing_configurations = false;
        }
      else if ( *cstr == "PERMISSIONS" )
        {
        if(current_match_rule)
          {
          doing_permissions_match = true;
          doing_permissions_file = false;
          }
        else
          {
          doing_permissions_match = false;
          doing_permissions_file = true;
          use_given_permissions_file = true;
          }
        doing_properties = false;
        doing_files = false;
        doing_permissions_dir = false;
        doing_components = false;
        doing_configurations = false;
        }
      else if ( *cstr == "DIR_PERMISSIONS" )
        {
        if(current_match_rule)
          {
          cmOStringStream e;
          e << "INSTALL does not allow \"" << *cstr << "\" after REGEX.";
          this->SetError(e.str().c_str());
          return false;
          }

        use_given_permissions_dir = true;
        doing_properties = false;
        doing_files = false;
        doing_permissions_file = false;
        doing_permissions_dir = true;
        doing_components = false;
        doing_configurations = false;
        }
      else if ( *cstr == "USE_SOURCE_PERMISSIONS" )
        {
        if(current_match_rule)
          {
          cmOStringStream e;
          e << "INSTALL does not allow \"" << *cstr << "\" after REGEX.";
          this->SetError(e.str().c_str());
          return false;
          }

        doing_properties = false;
        doing_files = false;
        doing_permissions_file = false;
        doing_permissions_dir = false;
        doing_components = false;
        doing_configurations = false;
        use_source_permissions = true;
        }
      else if ( *cstr == "COMPONENTS"  )
        {
        if(current_match_rule)
          {
          cmOStringStream e;
          e << "INSTALL does not allow \"" << *cstr << "\" after REGEX.";
          this->SetError(e.str().c_str());
          return false;
          }

        doing_properties = false;
        doing_files = false;
        doing_permissions_file = false;
        doing_permissions_dir = false;
        doing_components = true;
        doing_configurations = false;
        }
      else if ( *cstr == "CONFIGURATIONS"  )
        {
        if(current_match_rule)
          {
          cmOStringStream e;
          e << "INSTALL does not allow \"" << *cstr << "\" after REGEX.";
          this->SetError(e.str().c_str());
          return false;
          }

        doing_properties = false;
        doing_files = false;
        doing_permissions_file = false;
        doing_permissions_dir = false;
        doing_components = false;
        doing_configurations = true;
        }
      else if ( *cstr == "FILES" && !doing_files)
        {
        if(current_match_rule)
          {
          cmOStringStream e;
          e << "INSTALL does not allow \"" << *cstr << "\" after REGEX.";
          this->SetError(e.str().c_str());
          return false;
          }

        doing_files = true;
        doing_properties = false;
        doing_permissions_file = false;
        doing_permissions_dir = false;
        doing_components = false;
        doing_configurations = false;
        }
      else if ( doing_properties && i < args.size()-1 )
        {
        properties[args[i]] = args[i+1].c_str();
        i++;
        }
      else if ( doing_files )
        {
        files.push_back(*cstr);
        }
      else if ( doing_components )
        {
        components.insert(*cstr);
        }
      else if ( doing_configurations )
        {
        configurations.insert(cmSystemTools::UpperCase(*cstr));
        }
      else if(doing_permissions_file)
        {
        if(!installer.CheckPermissions(args[i], permissions_file))
          {
          return false;
          }
        }
      else if(doing_permissions_dir)
        {
        if(!installer.CheckPermissions(args[i], permissions_dir))
          {
          return false;
          }
        }
      else if(doing_permissions_match)
        {
        if(!installer.CheckPermissions(
            args[i], current_match_rule->Properties.Permissions))
          {
          return false;
          }
        }
      else
        {
        this->SetError("called with inappropriate arguments");
        return false;
        }
      }

    // now check and postprocess what has been parsed
    if ( files.size() == 0 )
      {
      this->SetError(
                     "called with inapropriate arguments. No FILES provided.");
      return false;
      }

    // Check rename form.
    if(!rename.empty())
      {
      if(itype != cmTarget::INSTALL_FILES &&
         itype != cmTarget::INSTALL_PROGRAMS)
        {
        this->SetError("INSTALL option RENAME may be used only with "
            "FILES or PROGRAMS.");
        return false;
        }
      if(files.size() > 1)
        {
        this->SetError("INSTALL option RENAME may be used only with "
                       "one file.");
        return false;
        }
      }

      if (this->HandleInstallDestination(installer, destination) == false)
      {
      return false;
      }

    this->GetTargetTypeFromString(stype, itype);

    this->HandleInstallPermissions(installer, 
                             permissions_file,
                             permissions_dir,
                             itype,
                             use_given_permissions_file,
                             use_given_permissions_dir,
                             use_source_permissions);

  return true;
}

//----------------------------------------------------------------------------
bool cmFileCommand::DoInstall( cmFileInstaller& installer,
                              const std::set<cmStdString>& components,
                              const std::set<cmStdString>& configurations,
                              std::map<cmStdString, const char*>& properties,
                              const int itype,
                              const std::string& rename,
                              const std::string& destination,
                              const std::vector<std::string>& files,
                              const bool optional)
{
  typedef std::set<cmStdString>::const_iterator iter_type;
  // Check for component-specific installation.
  const char* cmake_install_component =
    this->Makefile->GetDefinition("CMAKE_INSTALL_COMPONENT");
  if(cmake_install_component && *cmake_install_component)
    {
    // This install rule applies only if it is associated with the
    // current component.
    if(iter_type(components.find(cmake_install_component)) ==
       components.end())
      {
      return true;
      }
    }

  // Check for configuration-specific installation.
  if(!configurations.empty())
    {
    std::string cmake_install_configuration = cmSystemTools::UpperCase(
        this->Makefile->GetSafeDefinition("CMAKE_INSTALL_CONFIG_NAME"));
    if(cmake_install_configuration.empty())
      {
      // No configuration specified for installation but this install
      // rule is configuration-specific.  Skip it.
      return true;
      }
    else if(iter_type(configurations.find(cmake_install_configuration)) ==
            configurations.end())
      {
      // This rule is specific to a configuration not being installed.
      return true;
      }
    }

  // Check whether files should be copied always or only if they have
  // changed.
  bool copy_always =
    cmSystemTools::IsOn(cmSystemTools::GetEnv("CMAKE_INSTALL_ALWAYS"));

  // Handle each file listed.
  for (std::vector<std::string>::size_type i = 0; i < files.size(); i ++ )
    {
    // Split the input file into its directory and name components.
    std::vector<std::string> fromPathComponents;
    cmSystemTools::SplitPath(files[i].c_str(), fromPathComponents);
    std::string fromName = *(fromPathComponents.end()-1);
    std::string fromDir = cmSystemTools::JoinPath(fromPathComponents.begin(),
                                                  fromPathComponents.end()-1);

    // Compute the full path to the destination file.
    std::string toFile = destination;
    std::string const& toName = rename.empty()? fromName : rename;
    if(!toName.empty())
      {
      toFile += "/";
      toFile += toName;
      }

    // Handle type-specific installation details.
    switch(itype)
      {
      case cmTarget::MODULE_LIBRARY:
      case cmTarget::STATIC_LIBRARY:
      case cmTarget::SHARED_LIBRARY:
        {
        // Handle shared library versioning
        const char* lib_version = 0;
        const char* lib_soversion = 0;
        if ( properties.find("VERSION") != properties.end() )
          {
          lib_version = properties["VERSION"];
          }
        if ( properties.find("SOVERSION") != properties.end() )
          {
          lib_soversion = properties["SOVERSION"];
          }
        if ( !lib_version && lib_soversion )
          {
          lib_version = lib_soversion;
          }
        if ( !lib_soversion && lib_version )
          {
          lib_soversion = lib_version;
          }
        if ( lib_version && lib_soversion )
          {
          std::string libname = toFile;
          std::string soname = toFile;
          std::string soname_nopath = fromName;
          this->ComputeVersionedName(soname, lib_soversion);
          this->ComputeVersionedName(soname_nopath, lib_soversion);
          this->ComputeVersionedName(fromName, lib_version);
          this->ComputeVersionedName(toFile, lib_version);

          cmSystemTools::RemoveFile(soname.c_str());
          cmSystemTools::RemoveFile(libname.c_str());

          if (!cmSystemTools::CreateSymlink(soname_nopath.c_str(), 
                                            libname.c_str()) )
            {
            std::string errstring = "error when creating symlink from: " 
              + libname + " to " + soname_nopath;
            this->SetError(errstring.c_str());
            return false;
            }
          installer.ManifestAppend(libname);
          if ( toFile != soname )
            {
            if ( !cmSystemTools::CreateSymlink(fromName.c_str(), 
                                               soname.c_str()) )
              {
              std::string errstring = "error when creating symlink from: " 
                + soname + " to " + fromName;
              this->SetError(errstring.c_str());
              return false;
              }
            installer.ManifestAppend(soname);
            }
          }
        }
        break;
      case cmTarget::EXECUTABLE:
        {
        // Handle executable versioning
        const char* exe_version = 0;
        if ( properties.find("VERSION") != properties.end() )
          {
          exe_version = properties["VERSION"];
          }
        if ( exe_version )
          {
          std::string exename = toFile;
          std::string exename_nopath = fromName;
          exename_nopath += "-";
          exename_nopath += exe_version;

          fromName += "-";
          fromName += exe_version;
          toFile += "-";
          toFile += exe_version;

          cmSystemTools::RemoveFile(exename.c_str());

          if (!cmSystemTools::CreateSymlink(exename_nopath.c_str(), 
                                            exename.c_str()) )
            {
            std::string errstring = "error when creating symlink from: " 
              + exename + " to " + exename_nopath;
            this->SetError(errstring.c_str());
            return false;
            }
          installer.ManifestAppend(exename);
          }
        }
        break;
      }

    // Construct the full path to the source file.  The file name may
    // have been changed above.
    std::string fromFile = fromDir;
    if(!fromName.empty())
      {
      fromFile += "/";
      fromFile += fromName;
      }

    std::string message;
    if(!cmSystemTools::SameFile(fromFile.c_str(), toFile.c_str()))
      {
      if(itype == cmTarget::INSTALL_DIRECTORY &&
         (fromFile.empty() ||
          cmSystemTools::FileIsDirectory(fromFile.c_str())))
        {
        // Try installing this directory.
        if(!installer.InstallDirectory(fromFile.c_str(), toFile.c_str(),
                                       copy_always))
          {
          return false;
          }
        }
      else if(cmSystemTools::FileExists(fromFile.c_str()))
        {
        // Install this file.
        if(!installer.InstallFile(fromFile.c_str(), toFile.c_str(),
                                  copy_always))
          {
          return false;
          }
        }
      else if(!optional)
        {
        // The input file does not exist and installation is not optional.
        cmOStringStream e;
        e << "INSTALL cannot find file \"" << fromFile << "\" to install.";
        this->SetError(e.str().c_str());
        return false;
        }
      }
    }

  return true;
}

//----------------------------------------------------------------------------
void cmFileCommand::ComputeVersionedName(std::string& name,
                                         const char* version)
{
#if defined(__APPLE__)
  std::string ext;
  cmsys_stl::string::size_type dot_pos = name.rfind(".");
  if(dot_pos != name.npos)
    {
    ext = name.substr(dot_pos, name.npos);
    name = name.substr(0, dot_pos);
    }
#endif
  name += ".";
  name += version;
#if defined(__APPLE__)
  name += ext;
#endif
}

//----------------------------------------------------------------------------
bool cmFileCommand::HandleRelativePathCommand(
  std::vector<std::string> const& args)
{
  if(args.size() != 4 )
    {
    this->SetError("called with incorrect number of arguments");
    return false;
    }

  const std::string& outVar = args[1];
  const std::string& directoryName = args[2];
  const std::string& fileName = args[3];

  if(!cmSystemTools::FileIsFullPath(directoryName.c_str()))
    {
    std::string errstring = 
      "RelativePath must be passed a full path to the directory: " 
      + directoryName;
    this->SetError(errstring.c_str());
    return false;
    }
  if(!cmSystemTools::FileIsFullPath(fileName.c_str()))
    {
    std::string errstring = 
      "RelativePath must be passed a full path to the file: " 
      + fileName;
    this->SetError(errstring.c_str());
    return false;
    }

  std::string res = cmSystemTools::RelativePath(directoryName.c_str(), 
                                                fileName.c_str());
  this->Makefile->AddDefinition(outVar.c_str(),
    res.c_str());
  return true;
}


//----------------------------------------------------------------------------
bool cmFileCommand::HandleRemove(std::vector<std::string> const& args,
                                 bool recurse)
{

  std::string message;
  std::vector<std::string>::const_iterator i = args.begin();

  i++; // Get rid of subcommand
  for(;i != args.end(); ++i)
    {
    if(cmSystemTools::FileIsDirectory(i->c_str()) && recurse)
      {
      cmSystemTools::RemoveADirectory(i->c_str());
      }
    else
      {
      cmSystemTools::RemoveFile(i->c_str());
      }
    }
  return true;
}

//----------------------------------------------------------------------------
bool cmFileCommand::HandleCMakePathCommand(std::vector<std::string>
                                           const& args, 
                                           bool nativePath)
{
  std::vector<std::string>::const_iterator i = args.begin();
  if(args.size() != 3)
    {
    this->SetError("FILE(SYSTEM_PATH ENV result) must be called with "
                   "only three arguments.");
    return false;
    }
  i++; // Get rid of subcommand
#if defined(_WIN32) && !defined(__CYGWIN__)
  char pathSep = ';';
#else
  char pathSep = ':';
#endif
  std::vector<cmsys::String> path = cmSystemTools::SplitString(i->c_str(), 
                                                             pathSep);
  i++;
  const char* var =  i->c_str();
  std::string value;
  for(std::vector<cmsys::String>::iterator j = path.begin();
      j != path.end(); ++j)
    {
    if(j != path.begin())
      {
      value += ";";
      }
    if(!nativePath)
      {
      cmSystemTools::ConvertToUnixSlashes(*j);
      }
    else
      {
      *j = cmSystemTools::ConvertToOutputPath(j->c_str());
      // remove double quotes in the path
      cmsys::String& s = *j;
      
      if(s.size() > 1 && s[0] == '\"' && s[s.size()-1] == '\"')
        {
        s = s.substr(1,s.size()-2);
        }
      }
    value += *j;
    }
  this->Makefile->AddDefinition(var, value.c_str());
  return true;
}


