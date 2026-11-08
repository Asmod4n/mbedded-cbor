<?xml version="1.0" encoding="UTF-8"?>
<!-- Writes the qualified name of each class, enum and free function of namespace cbor, and of each specialization in namespace std, that has a brief description, from the Doxygen XML index, one per line. -->
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="text" encoding="UTF-8"/>

<xsl:template match="/">
  <xsl:for-each select="doxygenindex/compound[@kind='class' or @kind='struct'][(starts-with(name, 'cbor::') and not(contains(substring-after(name, 'cbor::'), '::'))) or starts-with(name, 'std::')]">
    <xsl:if test="normalize-space(document(concat(@refid, '.xml'), /)/doxygen/compounddef/briefdescription)">
      <xsl:value-of select="concat(name, '&#10;')"/>
    </xsl:if>
  </xsl:for-each>
  <xsl:for-each select="doxygenindex/compound[@kind='namespace' and name='cbor']/member[@kind='function' or @kind='enum']">
    <xsl:variable name="id" select="@refid"/>
    <xsl:if test="normalize-space(document(concat(../@refid, '.xml'), /)//memberdef[@id=$id]/briefdescription)">
      <xsl:value-of select="concat('cbor::', name, '&#10;')"/>
    </xsl:if>
  </xsl:for-each>
</xsl:template>

</xsl:stylesheet>
