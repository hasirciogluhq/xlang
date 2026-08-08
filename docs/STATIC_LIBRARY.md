# xlang Static Library Reference

This document provides a professional overview of the static library system in the xlang programming language. It explains how to manage, install, and use static libraries efficiently in your xlang projects.

---

## Table of Contents

- [xlang Static Library Reference](#xlang-static-library-reference)
  - [Table of Contents](#table-of-contents)
- [Overview](#overview)
- [Where to Place Static Libraries](#where-to-place-static-libraries)
- [Compiler Rules and Configuration](#compiler-rules-and-configuration)

---

# Overview

The static library system in xlang follows principles similar to the static libraries found in languages like C. At compile time, the appropriate static libraries are linked based on the target platform, and cross-compilation is fully supported.

---

# Where to Place Static Libraries

You do not need to manually place static libraries in specific directories. Instead, libraries are installed via the compiler, which registers them in your local library registry. There are two installation scopes:

- **Global:** Libraries are installed to the root directory, making them available system-wide.
- **Local:** Libraries are installed to the current user's home directory, making them available only to that user.

After installation, libraries can be imported directly into your project. During target compilation, the compiler will automatically link any required static libraries. 

> **Note:** You must compile libraries before installing them.

Naming conventions are important when creating new libraries. If there is a naming conflict (for example, two different publishers providing a library with the same name), you will receive a compile-time error. To avoid such conflicts, use qualified names like `john/network` or `zona/network`. The compiler will then be able to uniquely identify and link the correct library automatically.

---

# Compiler Rules and Configuration

Compiler rules regarding static libraries may evolve over time. Always refer to the latest documentation for up-to-date guidance. Additionally, the settings described here are governed by your active project configuration.

To add a static library to your project, use the following command:

```sh
xlang add <library/name@version> --static
```

- If the specified library is not already installed, the `add` command will automatically install it.
- By default, the library is installed locally. You can override this behavior using the `--root` or `--global` flags to install globally.
- The `--static` flag is required to ensure that the library is cached as a static library, provided that the package manager supports static libraries. These libraries will be stored in your project's cache directory and automatically linked during compilation.
- For instructions on clearing your project's cache, refer to the package manager documentation.

If both a library and its source code are added to your project, the source code will take precedence by default, and the library will be compiled from scratch during the build process. You can override this behavior in your project's configuration file. Please consult the configuration documentation for additional details.