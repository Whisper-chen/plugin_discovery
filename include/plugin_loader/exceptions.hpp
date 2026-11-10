/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
// Exception hierarchy. Same names and relationships as class_loader's.
#pragma once

#include <stdexcept>
#include <string>

namespace plugin_loader
{

class ClassLoaderException : public std::runtime_error
{
public:
  explicit ClassLoaderException(const std::string & error_desc)
  : std::runtime_error(error_desc) {}
};

// A shared library could not be opened.
class LibraryLoadException : public ClassLoaderException
{
public:
  explicit LibraryLoadException(const std::string & error_desc)
  : ClassLoaderException(error_desc) {}
};

// A shared library could not be closed.
class LibraryUnloadException : public ClassLoaderException
{
public:
  explicit LibraryUnloadException(const std::string & error_desc)
  : ClassLoaderException(error_desc) {}
};

// An object of the requested class could not be created.
class CreateClassException : public ClassLoaderException
{
public:
  explicit CreateClassException(const std::string & error_desc)
  : ClassLoaderException(error_desc) {}
};

// MultiLibraryClassLoader has no ClassLoader bound to the requested library.
class NoClassLoaderExistsException : public ClassLoaderException
{
public:
  explicit NoClassLoaderExistsException(const std::string & error_desc)
  : ClassLoaderException(error_desc) {}
};

}  // namespace plugin_loader
