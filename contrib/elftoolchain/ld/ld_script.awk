# Transform text file contents into a string literal.
#
# Usage: awk -f THIS-SCRIPT INPUT > OUTPUT
#
# $Id$

BEGIN {
    # Generate a symbol name based on the last component
    # of the input file name.
    split(ARGV[1], s, ".");
    sub(".*/", "", s[1]);
    printf "const char *%s = ", s[1];
}

# Enclose each line of text with a preceding and trailing '"',
# escaping any '"' characters that are present.
{
    printf "\"";
    gsub("\"", "\\\"");
    printf "%s\\n\"\n", $0;
}

END {
    print ";";
}
