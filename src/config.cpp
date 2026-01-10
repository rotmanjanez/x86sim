//
// PTLsim: Cycle Accurate x86-64 Simulator
// Configuration Management
//
// Copyright 2000-2008 Matt T. Yourst <yourst@yourst.com>
//

#include <fstream>
#include <sstream>
#include "config.h"
#include "logging.h"

std::string ConfigurationParserBase::format_to_string_usage(const void* baseptr) const {
  std::string result = "Options are:\n";
  ConfigurationOption* option = options;
  int maxlength = 0;
  while (option) {
    if likely (option->type != OPTION_TYPE_SECTION)
      maxlength = std::max(maxlength, int(strlen(option->name)));
    option = option->next;
  }

  option = options;
  while (option) {
    void* variable = (baseptr) ? ((void*)((Waddr)baseptr + option->offset)) : null;
    if (option->type == OPTION_TYPE_SECTION) {
      result += std::format("{}:\n", option->description);
      option = option->next;
      continue;
    }

    result += std::format("  -{:<{}} {} [", option->name, maxlength, option->description);

    switch (option->type) {
    case OPTION_TYPE_NONE:
      break;
    case OPTION_TYPE_W64: {
      W64 v = *((W64*)(variable));
      if (v == infinity)
        result += "inf";
      else
        result += std::format("{}", v);
      break;
    }
    case OPTION_TYPE_FLOAT:
      result += std::format("{}", *((double*)(variable)));
      break;
    case OPTION_TYPE_STRING:
      result += *(std::string*)variable;
      break;
    case OPTION_TYPE_BOOL:
      result += ((*(bool*)variable) ? "enabled" : "disabled");
      break;
    default:
      assert(false);
    }
    result += "]\n";
    option = option->next;
  }
  result += "\n";

  return result;
}

int ConfigurationParserBase::parse(void* baseptr, int argc, char* argv[]) {
  int i = 0;

  while (i < argc) {
    if ((argv[i][0] == '-') && strlen(argv[i]) > 1) {
      char* name = &argv[i][1];
      i++;
      bool found = false;

      if (0 == strcmp(name, "-"))
        return i;

      ConfigurationOption* option = options;
      while (option) {
        if (option->type == OPTION_TYPE_SECTION) {
          option = option->next;
          continue;
        }
        if (strequal(name, option->name)) {
          found = true;
          void* variable = (void*)((Waddr)baseptr + option->offset);
          if ((option->type != OPTION_TYPE_NONE) && (option->type != OPTION_TYPE_BOOL) && (i == (argc + 1))) {
            logging::eprintln("Warning: missing value for option '{}'", argv[i - 1]);
            break;
          }
          switch (option->type) {
          case OPTION_TYPE_NONE:
            break;
          case OPTION_TYPE_W64: {
            char* p = (i < argc) ? argv[i] : null;
            int len = (p) ? strlen(p) : 0;
            if (!len) {
              logging::eprintln("Warning: option {} had no argument; ignoring", argv[i - 1]);
              break;
            }

            char buf[256];
            strncpy(buf, p, sizeof(buf));
            p = buf;

            W64 multiplier = 1;
            char* endp = p;
            bool isinf = (strncmp(p, "inf", 3) == 0);
            if (len > 1) {
              char& c = p[len - 1];
              switch (c) {
              case 'k':
              case 'K':
                multiplier = 1000LL;
                c = 0;
                break;
              case 'm':
              case 'M':
                multiplier = 1000000LL;
                c = 0;
                break;
              case 'g':
              case 'G':
                multiplier = 1000000000LL;
                c = 0;
                break;
              case 't':
              case 'T':
                multiplier = 1000000000000LL;
                c = 0;
                break;
              }
            }
            W64 v = (isinf) ? infinity : strtoull(p, &endp, 0);
            if ((!isinf) && (endp[0] != 0)) {
              logging::eprintln("Warning: invalid value '{}' for option {}; ignoring", p, argv[i - 1]);
            }
            v *= multiplier;
            *((W64*)variable) = v;
            i++;

            break;
          }
          case OPTION_TYPE_FLOAT:
            if (i >= argc) {
              logging::eprintln("Warning: option {} had no argument; ignoring", argv[i - 1]);
              break;
            }
            *((double*)variable) = atof(argv[i++]);
            break;
          case OPTION_TYPE_BOOL:
            *((bool*)variable) = (!(*((bool*)variable)));
            break;
          case OPTION_TYPE_STRING: {
            if (i >= argc) {
              logging::eprintln("Warning: option {} had no argument; ignoring", argv[i - 1]);
              break;
            }
            std::string& s = *((std::string*)variable);
            s = argv[i++];
            break;
          }
          default:
            assert(false);
          }
          break;
        }

        option = option->next;
      }
      if (!found) {
        logging::eprintln("Warning: invalid option '{}'", (inrange(i - 1, 0, argc - 1) ? argv[i - 1] : "<missing>"));
        i++;
      }
    } else {
      return i; // trailing arguments, if any
    }
  }

  // no trailing arguments
  return -1;
}

std::string ConfigurationParserBase::format_to_string_config(const void* baseptr) const {
  std::string result = "Active parameters:\n";

  ConfigurationOption* option = options;
  while (option) {
    void* variable = (baseptr) ? ((void*)((Waddr)baseptr + option->offset)) : null;

    if (option->type == OPTION_TYPE_SECTION) {
      option = option->next;
      continue;
    }
    result += std::format("  -{:<12} ", option->name);
    switch (option->type) {
    case OPTION_TYPE_NONE:
    case OPTION_TYPE_SECTION:
      break;
    case OPTION_TYPE_W64: {
      W64 v = *((W64*)(variable));
      if (v == 0) {
        result += "0";
      } else if (v == infinity) {
        result += "infinity";
      } else if ((v % 1000000000LL) == 0) {
        result += std::format("{} G", (v / 1000000000LL));
      } else if ((v % 1000000LL) == 0) {
        result += std::format("{} M", (v / 1000000LL));
      } else {
        result += std::format("{}", v);
      }
      break;
    }
    case OPTION_TYPE_FLOAT:
      result += std::format("{}", *((double*)(variable)));
      break;
    case OPTION_TYPE_BOOL:
      result += (*((bool*)(variable)) ? "enabled" : "disabled");
      break;
    case OPTION_TYPE_STRING:
      result += *((std::string*)(variable));
      break;
    default:
      break;
    }
    result += "\n";

    option = option->next;
  }

  return result;
}
