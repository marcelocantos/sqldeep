// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0

package sqldeep

import (
	"errors"
	"os"
	"testing"

	"gopkg.in/yaml.v3"
)

// ── YAML schema (private, tags match YAML field names) ──────────────

type yamlConventionCase struct {
	Name             string `yaml:"name"`
	Input            string `yaml:"input"`
	Expected         string `yaml:"expected"`
	ExpectedPostgres string `yaml:"expected_postgres"`
}

type yamlFKCase struct {
	Name             string      `yaml:"name"`
	Input            string      `yaml:"input"`
	FKs              []yamlFK    `yaml:"fks"`
	Expected         string      `yaml:"expected"`
	ExpectedPostgres string      `yaml:"expected_postgres"`
}

type yamlFK struct {
	FromTable string           `yaml:"from_table"`
	ToTable   string           `yaml:"to_table"`
	Columns   []yamlColumnPair `yaml:"columns"`
}

type yamlColumnPair struct {
	FromColumn string `yaml:"from_column"`
	ToColumn   string `yaml:"to_column"`
}

type yamlErrorCase struct {
	Name       string  `yaml:"name"`
	Input      string  `yaml:"input"`
	ErrorLine  *int    `yaml:"error_line"`
	ErrorColGT *int    `yaml:"error_col_gt"`
}

type yamlFKErrorCase struct {
	Name  string   `yaml:"name"`
	Input string   `yaml:"input"`
	FKs   []yamlFK `yaml:"fks"`
}

type yamlTestData struct {
	Convention []yamlConventionCase `yaml:"convention"`
	FK         []yamlFKCase         `yaml:"fk"`
	Errors     []yamlErrorCase      `yaml:"errors"`
	FKErrors   []yamlFKErrorCase    `yaml:"fk_errors"`
}

func toFKs(yfks []yamlFK) []ForeignKey {
	fks := make([]ForeignKey, len(yfks))
	for i, yfk := range yfks {
		cols := make([]ColumnPair, len(yfk.Columns))
		for j, yc := range yfk.Columns {
			cols[j] = ColumnPair{FromColumn: yc.FromColumn, ToColumn: yc.ToColumn}
		}
		fks[i] = ForeignKey{FromTable: yfk.FromTable, ToTable: yfk.ToTable, Columns: cols}
	}
	return fks
}

func loadTestData(t *testing.T) yamlTestData {
	t.Helper()
	data, err := os.ReadFile("../../testdata/transpile.yaml")
	if err != nil {
		t.Fatalf("reading test data: %v", err)
	}
	var td yamlTestData
	if err := yaml.Unmarshal(data, &td); err != nil {
		t.Fatalf("parsing test data: %v", err)
	}
	return td
}

// ── Convention transpile tests ──────────────────────────────────────

func TestTranspile(t *testing.T) {
	td := loadTestData(t)
	for _, tc := range td.Convention {
		t.Run(tc.Name, func(t *testing.T) {
			got, err := Transpile(tc.Input)
			if err != nil {
				t.Fatalf("Transpile() error = %v", err)
			}
			if got != tc.Expected {
				t.Errorf("Transpile()\n got = %q\nwant = %q", got, tc.Expected)
			}
		})
	}
}

// ── FK transpile tests ──────────────────────────────────────────────

func TestTranspileFK(t *testing.T) {
	td := loadTestData(t)
	for _, tc := range td.FK {
		t.Run(tc.Name, func(t *testing.T) {
			got, err := TranspileFK(tc.Input, toFKs(tc.FKs))
			if err != nil {
				t.Fatalf("TranspileFK() error = %v", err)
			}
			if got != tc.Expected {
				t.Errorf("TranspileFK()\n got = %q\nwant = %q", got, tc.Expected)
			}
		})
	}
}

// ── PostgreSQL convention transpile tests ────────────────────────────

func TestTranspilePostgres(t *testing.T) {
	td := loadTestData(t)
	for _, tc := range td.Convention {
		if tc.ExpectedPostgres == "" {
			continue
		}
		t.Run(tc.Name, func(t *testing.T) {
			got, err := TranspilePostgres(tc.Input)
			if err != nil {
				t.Fatalf("TranspilePostgres() error = %v", err)
			}
			if got != tc.ExpectedPostgres {
				t.Errorf("TranspilePostgres()\n got = %q\nwant = %q", got, tc.ExpectedPostgres)
			}
		})
	}
}

// ── PostgreSQL FK transpile tests ───────────────────────────────────

func TestTranspileFKPostgres(t *testing.T) {
	td := loadTestData(t)
	for _, tc := range td.FK {
		if tc.ExpectedPostgres == "" {
			continue
		}
		t.Run(tc.Name, func(t *testing.T) {
			got, err := TranspileFKPostgres(tc.Input, toFKs(tc.FKs))
			if err != nil {
				t.Fatalf("TranspileFKPostgres() error = %v", err)
			}
			if got != tc.ExpectedPostgres {
				t.Errorf("TranspileFKPostgres()\n got = %q\nwant = %q", got, tc.ExpectedPostgres)
			}
		})
	}
}

// ── Convention error tests ──────────────────────────────────────────

func TestTranspileErrors(t *testing.T) {
	td := loadTestData(t)
	for _, tc := range td.Errors {
		t.Run(tc.Name, func(t *testing.T) {
			_, err := Transpile(tc.Input)
			if err == nil {
				t.Fatal("expected error, got nil")
			}
			var sdErr *Error
			if !errors.As(err, &sdErr) {
				t.Fatalf("expected *sqldeep.Error, got %T: %v", err, err)
			}
			if tc.ErrorLine != nil && sdErr.Line != *tc.ErrorLine {
				t.Errorf("Line = %d, want %d", sdErr.Line, *tc.ErrorLine)
			}
			if tc.ErrorColGT != nil && sdErr.Col <= *tc.ErrorColGT {
				t.Errorf("Col = %d, want > %d", sdErr.Col, *tc.ErrorColGT)
			}
		})
	}
}

// ── FK error tests ──────────────────────────────────────────────────

func TestTranspileFKErrors(t *testing.T) {
	td := loadTestData(t)
	for _, tc := range td.FKErrors {
		t.Run(tc.Name, func(t *testing.T) {
			_, err := TranspileFK(tc.Input, toFKs(tc.FKs))
			if err == nil {
				t.Fatal("expected error, got nil")
			}
			var sdErr *Error
			if !errors.As(err, &sdErr) {
				t.Fatalf("expected *sqldeep.Error, got %T: %v", err, err)
			}
		})
	}
}

// ── Programmatic tests (not in YAML) ────────────────────────────────

func TestExcessiveNestingDepth(t *testing.T) {
	input := ""
	for i := 0; i < 250; i++ {
		input += "SELECT { a: "
	}
	input += "1"
	for i := 0; i < 250; i++ {
		input += " } FROM t"
	}
	_, err := Transpile(input)
	if err == nil {
		t.Fatal("expected error for excessive nesting, got nil")
	}
}

func TestVersion(t *testing.T) {
	v := Version()
	if v == "" {
		t.Fatal("Version() returned empty string")
	}
	t.Logf("version = %s", v)
}
