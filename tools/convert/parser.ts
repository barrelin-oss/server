export type FieldType = 'int' | 'string' | 'bool' | 'price';

export interface FieldDef {
    name: string;
    type: FieldType;
    optional?: boolean;  // if true, missing at end of record is ok (emitted as undefined)
}

export interface CfgSchema {
    trigger: string;
    end_marker?: string;
    fields: FieldDef[];
}

export type CfgRecord = Record<string, unknown>;

// Splits content into tokens using the same separators as Game.cpp CStrTok.
// seps = "= \t\n\r" — splits on equals, space, tab, newline, carriage return.
function tokenize(content: string): string[] {
    return content
        .split(/[= \t\n\r]+/)
        .filter(t => t.length > 0 && !t.startsWith('//'));
}

// Strips line comments from content before tokenizing.
function strip_comments(content: string): string {
    return content
        .split('\n')
        .map(line => {
            const ci = line.indexOf('//');
            return ci >= 0 ? line.slice(0, ci) : line;
        })
        .join('\n');
}

function parse_value(token: string, type: FieldType): unknown {
    switch (type) {
        case 'int':   return parseInt(token, 10);
        case 'string': return token;
        case 'bool':  return token.toUpperCase() === 'TRUE';
        case 'price': {
            const v = parseInt(token, 10);
            return v;  // caller handles is_for_sale logic
        }
    }
}

export function parse_cfg(content: string, schema: CfgSchema): CfgRecord[] {
    const clean = strip_comments(content);
    const tokens = tokenize(clean);
    const records: CfgRecord[] = [];

    let i = 0;
    while (i < tokens.length) {
        const tok = tokens[i];

        if (schema.end_marker && tok === schema.end_marker) break;

        // Check for trigger (case-sensitive, prefix match like Game.cpp's memcmp)
        if (tok === schema.trigger) {
            i++;
            const record: CfgRecord = {};

            for (const field of schema.fields) {
                if (i >= tokens.length) {
                    if (field.optional) break;
                    throw new Error(
                        `Ran out of tokens reading field '${field.name}' ` +
                        `(record so far: ${JSON.stringify(record)})`
                    );
                }
                // For optional fields, stop if the next token is a new record trigger or end marker.
                if (field.optional && (tokens[i] === schema.trigger || tokens[i] === schema.end_marker)) break;
                const raw = tokens[i++];
                const value = parse_value(raw, field.type);

                if (field.type === 'price') {
                    const n = value as number;
                    if (n < 0) {
                        record['is_for_sale'] = false;
                        record[field.name] = Math.abs(n);
                    } else {
                        record[field.name] = n;
                    }
                } else {
                    record[field.name] = value;
                }
            }

            records.push(record);
        } else {
            i++;
        }
    }

    return records;
}
